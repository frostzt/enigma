#ifndef ENIGMA_DB_LOG_H
#define ENIGMA_DB_LOG_H

#include <pthread.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace enigmadb {

enum class Level : uint8_t { Trace, Debug, Info, Warn, Error, Fatal, Off };
enum class Category : uint8_t {
    General,
    Wal,
    SSTable,
    Memtable,
    Compaction,
    IO,
    _Count
};

#ifndef NDEBUG
inline constexpr Level kCompileMinLevel = Level::Info;
#else
inline constexpr Level kCompileMinLevel = Level::Trace;
#endif

enum class OverflowPolicy { Block, DropNewest, BlockOnWarnPlus };

extern std::atomic<Level> g_levels[static_cast<size_t>(Category::_Count)];

struct LogRecord {
    Level level;
    Category category;
    std::chrono::system_clock::time_point ts;
    uint64_t tid;
    const char* file;
    int line;
    const char* func;
    std::string message;
};

struct LogConfig {
    Level default_level = Level::Info;
    std::array<Level, static_cast<size_t>(Category::_Count)> category_levels{};
    bool async = false;
    size_t queue_capacity = 8192;
    OverflowPolicy overflow = OverflowPolicy::BlockOnWarnPlus;
    std::optional<std::string> file_path;
    bool console = true;
    bool enable_crash_handler = false;
    size_t ring_slots = 1024;
    size_t ring_slot_bytes = 512;

    LogConfig();
};

inline uint64_t get_current_thread_id() noexcept {
#if defined(__APPLE__)
    uint64_t tid;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#elif defined(__linux__)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#else
    return static_cast<uint64_t>(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
};

class LogSink;

class Logger {
   public:
    static Logger& instance();

    void init(const LogConfig& config);
    void shutdown();
    void dispatch(LogRecord record);

    [[nodiscard]] bool is_enabled(Category cat, Level lvl) const noexcept {
        if (lvl < kCompileMinLevel) return false;
        const auto c = static_cast<size_t>(cat);
        return lvl >= active_levels_[c].load(std::memory_order_relaxed);
    }

    void add_sink(std::shared_ptr<LogSink> sink);
    void set_level(Category cat, Level lvl) noexcept;
    void dump_ring_buffer_to_stderr();

   private:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::array<std::atomic<Level>, static_cast<size_t>(Category::_Count)>
        active_levels_{};
    std::vector<std::shared_ptr<LogSink>> sinks_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> shutdown_{false};
};

inline bool log_enabled(Category cat, Level lvl) noexcept {
    return Logger::instance().is_enabled(cat, lvl);
};

template <typename... Args>
void log_impl(Category cat, Level lvl, const char* file, int line,
              const char* fn, std::format_string<Args...> fmt, Args&&... args) {
    thread_local std::string tls_buffer;
    tls_buffer.clear();

    try {
        std::vformat_to(std::back_inserter(tls_buffer), fmt.get(),
                        std::make_format_args(args...));
    } catch (...) {
        tls_buffer = "<log formatting error>";
    }

    LogRecord record{.level = lvl,
                     .category = cat,
                     .ts = std::chrono::system_clock::now(),
                     .tid = get_current_thread_id(),
                     .file = file,
                     .line = line,
                     .func = fn,
                     .message = tls_buffer};

    Logger::instance().dispatch(std::move(record));
}

void install_crash_handlers();

}  // namespace enigmadb

#define ENIGMA_LOG(cat, lvl, ...)                                            \
    do {                                                                     \
        if (::enigmadb::log_enabled((cat), (lvl))) [[unlikely]] {            \
            ::enigmadb::log_impl((cat), (lvl), __FILE__, __LINE__, __func__, \
                                 __VA_ARGS__);                               \
        }                                                                    \
    } while (0)

#define LOG_TRACE(cat, ...) \
    ENIGMA_LOG(cat, ::enigmadb::Level::Trace, __VA_ARGS__)
#define LOG_DEBUG(cat, ...) \
    ENIGMA_LOG(cat, ::enigmadb::Level::Debug, __VA_ARGS__)
#define LOG_INFO(cat, ...) ENIGMA_LOG(cat, ::enigmadb::Level::Info, __VA_ARGS__)
#define LOG_WARN(cat, ...) ENIGMA_LOG(cat, ::enigmadb::Level::Warn, __VA_ARGS__)
#define LOG_ERROR(cat, ...) \
    ENIGMA_LOG(cat, ::enigmadb::Level::Error, __VA_ARGS__)
#define LOG_FATAL(cat, ...) \
    ENIGMA_LOG(cat, ::enigmadb::Level::Fatal, __VA_ARGS__)

#endif  // ENIGMA_DB_LOG_H
