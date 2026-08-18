#include "enigmadb/error.h"

#include <execinfo.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "enigmadb/log.h"

namespace enigmadb {

[[noreturn]] void _server_panic_impl(const char* file, int line, const std::string& msg) {
    const char* trace_header =
        "\n=======================================================\nStack "
        "Trace:\n";
    ::write(STDERR_FILENO, trace_header, ::strlen(trace_header));

    void* callstack[32];
    int frames = ::backtrace(callstack, 32);
    ::backtrace_symbols_fd(callstack, frames, STDERR_FILENO);

    const char* trace_footer = "=======================================================\n";
    ::write(STDERR_FILENO, trace_footer, ::strlen(trace_footer));

    // LOG_FATAL handles formatted output, ring buffer dump, sink flushing, and
    // abort()
    ::enigmadb::log_impl(Category::General, Level::Fatal, file, line, "server_panic", "SERVER PANIC: {}", msg);

    // Unreachable, as LOG_FATAL calls std::abort() inside Logger::dispatch
    std::abort();
}

}  // namespace enigmadb
