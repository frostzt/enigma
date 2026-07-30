#include "enigmadb/common/error.h"

#include <cxxabi.h>

#include <iostream>
#include <string>

namespace enigmadb::common {

[[noreturn]] void _server_panic_impl(const char* file, int line,
                                     const std::string& msg) {
    std::cerr << "\n=======================================================\n";
    std::cerr << "!!! SERVER PANIC !!!\n";
    std::cerr << "Location: " << file << ":" << line << "\n";
    std::cerr << "Reason: " << msg << "\n";
    std::cerr << "=======================================================\n";
    std::cerr << "Stack Trace:\n";

    void* callstack[32];
    int frames = backtrace(callstack, 32);
    char** symbols = backtrace_symbols(callstack, frames);

    if (symbols != nullptr) {
        for (int i = 0; i < frames; i++) {
            std::string symbol_str(symbols[i]);

            size_t begin_mangled = symbol_str.find_first_of("( ");
            size_t end_mangled = symbol_str.find_first_of("+)", begin_mangled);

            if (begin_mangled != std::string::npos &&
                end_mangled != std::string::npos &&
                begin_mangled < end_mangled) {
                if (symbol_str[begin_mangled] == '(') begin_mangled++;
                std::string mangled = symbol_str.substr(
                    begin_mangled, end_mangled - begin_mangled);

                int status = 0;
                char* demangled = abi::__cxa_demangle(mangled.c_str(), nullptr,
                                                      nullptr, &status);

                if (status == 0 && demangled != nullptr) {
                    std::cerr << "  " << demangled << "\n";
                    std::free(demangled);
                    continue;
                }
            }
        }
        free(symbols);
    } else {
        std::cerr << "  (Failed to capture stack trace symbols)\n";
    }
    std::cerr << "=======================================================\n";

    std::cerr << "Aborting process immediately.\n" << std::endl;
    std::abort();
}

}  // namespace enigmadb::common
