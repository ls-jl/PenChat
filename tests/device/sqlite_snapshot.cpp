// Consistent snapshot of this app's live DB. The caller verifies the target
// SQLite shared library hash before executing this helper.
#include <sqlite3.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>

int main(int argc, char **argv) {
  if (argc != 3) return 2;
  umask(0077);
  const int fd = open(argv[2], O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (fd < 0) return 3;
  close(fd);
  sqlite3 *source = nullptr, *destination = nullptr;
  int result = sqlite3_open_v2(argv[1], &source, SQLITE_OPEN_READONLY | SQLITE_OPEN_NOMUTEX, nullptr);
  if (result == SQLITE_OK) {
    sqlite3_busy_timeout(source, 2000);
    result = sqlite3_open_v2(argv[2], &destination, SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX, nullptr);
  }
  if (result == SQLITE_OK) {
    sqlite3_backup *backup = sqlite3_backup_init(destination, "main", source, "main");
    if (!backup) result = sqlite3_errcode(destination);
    else {
      result = sqlite3_backup_step(backup, -1);
      const int finished = sqlite3_backup_finish(backup);
      if (result == SQLITE_DONE) result = finished;
    }
  }
  if (destination) sqlite3_close(destination);
  if (source) sqlite3_close(source);
  if (result != SQLITE_OK) {
    unlink(argv[2]);
    return 4;
  }
  puts("snapshot-ready");
  return 0;
}
