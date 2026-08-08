#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

#include <cstring>

#include "enigmadb/log.h"

namespace enigmadb {

static void crash_signal_handler(int sig, siginfo_t*, void*) {
    const char* msg = "\n!!! FATAL SIGNAL RECEIVED !!!\n";
    ::write(STDERR_FILENO, msg, std::strlen(msg));

    Logger::instance().dump_ring_buffer_to_stderr();

    void* array[32];
    int size = ::backtrace(array, 32);
    ::backtrace_symbols_fd(array, size, STDERR_FILENO);

    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void install_crash_handlers() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = crash_signal_handler;
    sa.sa_flags = SA_SIGINFO | SA_RESETHAND;

    ::sigaction(SIGSEGV, &sa, nullptr);
    ::sigaction(SIGABRT, &sa, nullptr);
    ::sigaction(SIGBUS, &sa, nullptr);
    ::sigaction(SIGFPE, &sa, nullptr);
    ::sigaction(SIGILL, &sa, nullptr);
}

}  // namespace enigmadb
