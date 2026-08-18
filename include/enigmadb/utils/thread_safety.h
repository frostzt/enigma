/* TODO: THIS IS NOT COMPLETE :) DO NOT USE IT @frostzt to wrap this up */

#ifndef ENIGMADB_UTILS_THREAD_SAFETY_H_
#define ENIGMADB_UTILS_THREAD_SAFETY_H_

#if defined(__clang__)
#define THREAD_ANNOTATION_ATTRIBUTE__(x) __attribute__((x))
#else
#define THREAD_ANNOTATION_ATTRIBUTE__(x)
#endif

#define GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(guarded_by(x))

#define PT_GUARDED_BY(x) THREAD_ANNOTATION_ATTRIBUTE__(pt_guided_by(x))

#define REQUIRES(...) THREAD_ANNOTATION_ATTRIBUTE__(requires_capability(__VA_ARGS__))

#define REQUIRES_SHARED(...) THREAD_ANNOTATION_ATTRIBUTE__(requires_shared_capability(__VA_ARGS__))

#define ACQUIRED_BEFORE(...) THREAD_ANNOTATION_ATTRIBUTE__(acquired_before(__VA_ARGS__))

#define ACQUIRED_AFTER(...) THREAD_ANNOTATION_ATTRIBUTE__(acquired_after(__VA_ARGS__))

#define CAPABILITY(x) THREAD_ANNOTATION_ATTRIBUTE__(capability(x))
#define ACQUIRE(...) THREAD_ANNOTATION_ATTRIBUTE__(acquire_capability(__VA_ARGS__))
#define RELEASE(...) THREAD_ANNOTATION_ATTRIBUTE__(release_capability(__VA_ARGS__))

#endif  // ENIGMADB_UTILS_THREAD_SAFETY_H_
