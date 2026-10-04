#ifndef ENIGMADB_COMMON_H_
#define ENIGMADB_COMMON_H_

namespace enigmadb {

#if defined(__GNUC__) || defined(__clang__)
#define ENIGMADB_UNREACHABLE() __builtin_unreachable()
#else
#define ENIGMADB_UNREACHABLE() \
    do {                       \
    } while (false)
#endif

#define DELETE_CLASS_COPY(classname)      \
    classname(const classname&) = delete; \
    classname& operator=(const classname& o) = delete;

#define CLASS_DEFAULT_MOVE(classname) \
    classname(classname&&) = default; \
    classname& operator=(classname&&) = default;

}  // namespace enigmadb

#endif  // ENIGMADB_COMMON_H_
