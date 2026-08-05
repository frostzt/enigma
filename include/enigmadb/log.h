#ifndef ENIGMA_DB_LOG_H
#define ENIGMA_DB_LOG_H

#include <atomic>
#include <chrono>
#include <format>
#include <string>

namespace enigmadb {

enum class Level { Trace, Debug, Info, Warn, Error, Fatal, Off };
enum class Category { General, Wal, SSTable, Memtable, Compaction, IO, _Count };

extern std::atomic<Level> g_levels[static_cast<size_t>(Category::_Count)];

struct LogRecord {
    Level level;
    Category category;
    std::chrono::system_clock::time_point ts;
    /// thread id
    uint64_t tid;
    const char* file;
    int line;
    const char* func;
    std::string message;
};

#define ENIGMA_LOG(cat, lvl, ...)                                            \
    do {                                                                     \
        if (::enigmadb::log_enabled((cat), (lvl)))                           \
            ::enigmadb::log_impl((cat), (lvl), __FILE__, __LINE__, __func__, \
                                 __VA_ARGS__);                               \
    } while (0)

inline void log_enabled(Category cat, Level lvl) {}

template <typename... Args>
void log_impl(Category cat, Level lvl, const char* file, int line,
              const char* fn, std::format_string<Args...> fmt, Args&&... args) {
}

}  // namespace enigmadb

#endif  // ENIGMA_DB_LOG_H
