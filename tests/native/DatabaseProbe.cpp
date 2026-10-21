#include "database.hpp"
#include <iostream>
#include <sqlite3.h>
#include <sys/stat.h>
#include <unistd.h>
using namespace penchat;
// Test/deployment helper: migrate an isolated DB copy, never start UI/networking.
int main(int argc, char **argv) {
  if (argc == 4 && std::string(argv[1]) == "--backup") {
    sqlite3 *source = nullptr, *target = nullptr;
    int rc = sqlite3_open_v2(argv[2], &source, SQLITE_OPEN_READONLY, nullptr);
    if (rc == SQLITE_OK)
      rc = sqlite3_open(argv[3], &target);
    if (rc == SQLITE_OK) {
      auto copy = sqlite3_backup_init(target, "main", source, "main");
      rc = SQLITE_ERROR;
      if (copy) {
        const auto deadline = ticks() + 5000;
        do {
          rc = sqlite3_backup_step(copy, -1);
          if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED)
            usleep(20000);
        } while ((rc == SQLITE_BUSY || rc == SQLITE_LOCKED) && ticks() < deadline);
        const auto finished = sqlite3_backup_finish(copy);
        if (finished != SQLITE_OK)
          rc = finished;
      }
    }
    if (target)
      sqlite3_close(target);
    if (source)
      sqlite3_close(source);
    if (rc != SQLITE_DONE)
      return 1;
    chmod(argv[3], 0600);
    return 0;
  }
  if (argc != 2)
    return 2;
  try {
    const auto start = ticks();
    Database db(argv[1]);
    db.flush();
    auto result = db.summary();
    result["schema"] = db.query(
        "SELECT value FROM schema_meta WHERE key='schema_version'")[0]["value"];
    result["integrity"] = db.query("PRAGMA quick_check")[0].begin().value();
    result["foreign_key_errors"] = db.query("PRAGMA foreign_key_check").size();
    result["elapsed_ms"] = ticks() - start;
    std::cout << result.dump() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
