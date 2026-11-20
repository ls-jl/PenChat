#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "elf_slot.hpp"
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <elf.h>
#include <fstream>
#include <link.h>
#include <openssl/sha.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
#include <vector>

namespace penchat_compositor {
namespace {
struct Module {
  std::uintptr_t base = 0;
  const ElfW(Phdr)* phdr = nullptr;
  std::size_t count = 0;
  std::string path;
};
bool contains(const Module& m, std::uintptr_t address, std::size_t size, int flags = PF_R) {
  if (!size || address > UINTPTR_MAX - size) return false;
  for (std::size_t i = 0; i < m.count; ++i) {
    const auto& p = m.phdr[i];
    if (p.p_type != PT_LOAD || (p.p_flags & flags) != unsigned(flags)) continue;
    const auto begin = m.base + p.p_vaddr;
    if (begin > UINTPTR_MAX - p.p_memsz) continue;
    if (address >= begin && address + size <= begin + p.p_memsz) return true;
  }
  return false;
}
std::uintptr_t pointer(const Module& m, std::uintptr_t value, std::size_t size) {
  if (contains(m, value, size)) return value;
  if (value <= UINTPTR_MAX - m.base && contains(m, m.base + value, size)) return m.base + value;
  throw std::runtime_error("ELF pointer outside readable image");
}
int permission(std::uintptr_t address) {
  std::ifstream maps("/proc/self/maps");
  std::string line;
  while (std::getline(maps, line)) {
    unsigned long begin = 0, end = 0;
    char p[5]{};
    if (std::sscanf(line.c_str(), "%lx-%lx %4s", &begin, &end, p) != 3) continue;
    if (address >= begin && address + sizeof(std::uintptr_t) <= end)
      return (p[0] == 'r' ? PROT_READ : 0) | (p[1] == 'w' ? PROT_WRITE : 0) | (p[2] == 'x' ? PROT_EXEC : 0);
  }
  return 0;
}
void validateHash(const std::string& path, const char* expected) {
  if (!expected) return;
  std::ifstream file(path, std::ios::binary);
  if (!file) throw std::runtime_error("Cannot read target image identity");
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  char buffer[16384];
  std::size_t size = 0;
  while (file) {
    file.read(buffer, sizeof(buffer));
    auto read = file.gcount();
    size += read;
    if (size > 16 * 1024 * 1024) throw std::runtime_error("Target image exceeds identity limit");
    if (read > 0) SHA256_Update(&ctx, buffer, read);
  }
  if (!file.eof()) throw std::runtime_error("Target image identity read failed");
  unsigned char digest[32];
  SHA256_Final(digest, &ctx);
  char hex[65]{};
  for (unsigned i = 0; i < 32; ++i) std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
  if (std::strcmp(hex, expected)) throw std::runtime_error("Target image SHA256 mismatch; no hook installed");
}
#if __SIZEOF_POINTER__ == 4
unsigned type(ElfW(Xword) info) { return ELF32_R_TYPE(info); }
unsigned index(ElfW(Xword) info) { return ELF32_R_SYM(info); }
constexpr unsigned kJumpSlot = R_ARM_JUMP_SLOT;
#else
unsigned type(ElfW(Xword) info) { return ELF64_R_TYPE(info); }
unsigned index(ElfW(Xword) info) { return ELF64_R_SYM(info); }
constexpr unsigned kJumpSlot = R_X86_64_JUMP_SLOT;
#endif
}
Slot locate(const char* name, const char* symbol, const char* expectedHash) {
  struct Search { const char* name; Module result; unsigned matches = 0; } search{name, {}, 0};
  dl_iterate_phdr([](dl_phdr_info* info, std::size_t, void* opaque) {
    auto& s = *static_cast<Search*>(opaque);
    const char* path = info->dlpi_name;
    const char* slash = std::strrchr(path, '/');
    if (std::strcmp(slash ? slash + 1 : path, s.name)) return 0;
    ++s.matches;
    s.result = {info->dlpi_addr, info->dlpi_phdr, info->dlpi_phnum, path};
    return 0;
  }, &search);
  if (search.matches != 1) throw std::runtime_error("Expected exactly one loaded target image");
  const auto& m = search.result;
  validateHash(m.path, expectedHash);
  const ElfW(Dyn)* dynamic = nullptr;
  std::size_t dynamicCount = 0;
  for (std::size_t i = 0; i < m.count; ++i) if (m.phdr[i].p_type == PT_DYNAMIC) {
    dynamic = reinterpret_cast<const ElfW(Dyn)*>(m.base + m.phdr[i].p_vaddr);
    dynamicCount = m.phdr[i].p_memsz / sizeof(ElfW(Dyn));
  }
  if (!dynamic || dynamicCount > 4096 || !contains(m, reinterpret_cast<std::uintptr_t>(dynamic), dynamicCount * sizeof(*dynamic)))
    throw std::runtime_error("Invalid ELF dynamic section");
  std::uintptr_t symtab = 0, strtab = 0, jumps = 0;
  std::size_t stringSize = 0, jumpBytes = 0, entrySize = 0;
  unsigned form = 0;
  bool end = false;
  for (std::size_t i = 0; i < dynamicCount; ++i) {
    const auto& d = dynamic[i];
    if (d.d_tag == DT_NULL) { end = true; break; }
    switch (d.d_tag) {
      case DT_SYMTAB: symtab = d.d_un.d_ptr; break;
      case DT_STRTAB: strtab = d.d_un.d_ptr; break;
      case DT_STRSZ: stringSize = d.d_un.d_val; break;
      case DT_SYMENT: entrySize = d.d_un.d_val; break;
      case DT_JMPREL: jumps = d.d_un.d_ptr; break;
      case DT_PLTRELSZ: jumpBytes = d.d_un.d_val; break;
      case DT_PLTREL: form = d.d_un.d_val; break;
    }
  }
  const auto relSize = form == DT_REL ? sizeof(ElfW(Rel)) : sizeof(ElfW(Rela));
  if (!end || !symtab || !strtab || !jumps || !stringSize || stringSize > 16 * 1024 * 1024 ||
      entrySize != sizeof(ElfW(Sym)) || (form != DT_REL && form != DT_RELA) ||
      !jumpBytes || jumpBytes > 2 * 1024 * 1024 || jumpBytes % relSize)
    throw std::runtime_error("Invalid ELF PLT metadata");
  strtab = pointer(m, strtab, stringSize);
  symtab = pointer(m, symtab, sizeof(ElfW(Sym)));
  jumps = pointer(m, jumps, jumpBytes);
  Slot slot;
  unsigned found = 0;
  for (std::size_t offset = 0; offset < jumpBytes; offset += relSize) {
    std::uintptr_t target;
    ElfW(Xword) info;
    if (form == DT_REL) {
      const auto& r = *reinterpret_cast<const ElfW(Rel)*>(jumps + offset); target = r.r_offset; info = r.r_info;
    } else {
      const auto& r = *reinterpret_cast<const ElfW(Rela)*>(jumps + offset); target = r.r_offset; info = r.r_info;
    }
    if (type(info) != kJumpSlot) continue;
    const std::size_t n = index(info);
    if (n > 1000000 || n > (UINTPTR_MAX - symtab) / sizeof(ElfW(Sym))) throw std::runtime_error("ELF symbol limit");
    const auto address = symtab + n * sizeof(ElfW(Sym));
    if (!contains(m, address, sizeof(ElfW(Sym)))) throw std::runtime_error("ELF symbol outside image");
    const auto& sym = *reinterpret_cast<const ElfW(Sym)*>(address);
    if (sym.st_name >= stringSize) throw std::runtime_error("Invalid ELF string offset");
    const auto* text = reinterpret_cast<const char*>(strtab + sym.st_name);
    const auto remaining = stringSize - sym.st_name;
    if (!std::memchr(text, 0, remaining)) throw std::runtime_error("Unterminated ELF symbol");
    if (std::strcmp(text, symbol)) continue;
    const auto ptr = m.base + target;
    if (ptr < m.base || !contains(m, ptr, sizeof(std::uintptr_t)) || ptr % alignof(std::uintptr_t))
      throw std::runtime_error("Invalid ELF slot alignment/range");
    slot.address = reinterpret_cast<std::uintptr_t*>(ptr);
    slot.original = __atomic_load_n(slot.address, __ATOMIC_SEQ_CST);
    slot.protection = permission(ptr);
    ++found;
  }
  if (found != 1 || !(slot.protection & PROT_READ) || slot.protection & PROT_EXEC)
    throw std::runtime_error("Expected one readable non-executable PLT slot");
  // Resolve through this image. Never redirect an unresolved lazy-binding thunk.
  void* handle = dlopen(m.path.c_str(), RTLD_NOW | RTLD_NOLOAD);
  if (!handle) throw std::runtime_error("Target image is no longer loaded");
  const auto resolved = reinterpret_cast<std::uintptr_t>(dlsym(handle, symbol));
  dlclose(handle);
  if (!resolved || slot.original != resolved || !contains(m, resolved, 4, PF_R | PF_X))
    throw std::runtime_error("PLT slot is unresolved or changed by another owner");
  return slot;
}
bool Slot::replace(std::uintptr_t expected, std::uintptr_t replacement, std::string& error) const {
  const long pageSize = sysconf(_SC_PAGESIZE);
  if (!address || !expected || !replacement || pageSize <= 0 || (pageSize & (pageSize - 1))) {
    error = "Invalid slot exchange"; return false;
  }
  auto* page = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(address) & ~(std::uintptr_t(pageSize) - 1));
  const bool change = !(protection & PROT_WRITE);
  if (change && mprotect(page, pageSize, protection | PROT_WRITE)) { error = "Cannot make GOT page writable"; return false; }
  const bool swapped = __atomic_compare_exchange_n(address, &expected, replacement, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
  if (change && mprotect(page, pageSize, protection)) {
    // Keep a transparent original path if restoring RELRO fails.
    if (swapped) {
      auto ours = replacement;
      __atomic_compare_exchange_n(address, &ours, original, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    }
    mprotect(page, pageSize, protection);
    error = "Cannot restore GOT page protection"; return false;
  }
  if (!swapped) error = "GOT ownership changed; refusing overwrite";
  return swapped;
}
}
