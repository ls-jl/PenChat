"""Policy for application-owned native payloads; no shell interpreter needed."""
import struct
import hashlib

FORBIDDEN = {
    'system', '__libc_system', 'popen', 'popen64', 'pclose', 'wordexp',
    'execlp', 'execvp', 'execvpe', 'posix_spawnp',
}


def check_native_payload(data, name):
    assert data[:6] == b'\x7fELF\x02\x01', (name, 'ELF64 little-endian required')
    offset = struct.unpack_from('<Q', data, 40)[0]
    entry, count = struct.unpack_from('<HH', data, 58)
    assert entry == 64 and count and offset + count * entry <= len(data), name
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, offset + i * entry)
                for i in range(count)]
    symbols = set()
    defined = set()
    for section in sections:
        if section[1] not in (2, 11):  # SHT_SYMTAB / SHT_DYNSYM
            continue
        start, size, link, symbol_size = section[4], section[5], section[6], section[9]
        assert symbol_size == 24 and size % symbol_size == 0 and link < count, name
        assert start + size <= len(data), name
        strings = sections[link]
        table = data[strings[4]:strings[4] + strings[5]]
        assert len(table) == strings[5], name
        for index in range(start, start + size, symbol_size):
            string_offset = struct.unpack_from('<I', data, index)[0]
            assert string_offset < len(table), name
            end = table.find(b'\0', string_offset)
            assert end >= 0, name
            symbol = table[string_offset:end].decode('ascii')
            symbols.add(symbol)
            if struct.unpack_from('<H', data, index + 6)[0] != 0:
                defined.add(symbol)
    assert symbols, (name, 'missing symbol table')
    forbidden = symbols & FORBIDDEN
    if name == 'assets/scanner/lib/librknnrt-y07.so':
        # Only this exact vendor artifact may import system. Its Y07 worker
        # exports the tested no-execution stub before loading this library.
        assert hashlib.sha256(data).hexdigest() == '9c5e43179e83f82b3ec1f2c2f9b520559af23ab76327aca63500db30d63d6910'
        forbidden -= {'system'}
    if name in ('scanner', 'penchat-scanner', 'assets/bin/penchat-scanner'):
        assert 'system' in defined, (name, 'missing vendor command blocker')
        assert 'ioctl' in defined, (name, 'missing scoped vendor DMA synchronization adapter')
        forbidden -= {'system'}
    assert not forbidden, (name, sorted(forbidden))
    for interpreter in (b'/bin/sh\0', b'/bin/bash\0', b'/system/bin/sh\0'):
        assert interpreter not in data, (name, 'shell interpreter path')
