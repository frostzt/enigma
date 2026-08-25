#ifndef ENIGMADB_TEST_SUPPORT_MACROS_H_
#define ENIGMADB_TEST_SUPPORT_MACROS_H_

/// ASan EXISTS
#if defined(__SANITIZE_ADDRESS__) || (defined(__has_feature) && __has_feature(address_sanitizer))
#define ASAN_ENABLED 1
#else
#define ASAN_ENABLED 0
#endif

#endif  // ENIGMADB_TEST_SUPPORT_MACROS_H_
