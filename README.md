# enigma

## Directory Architecture

```shell
enigmadb/
├── CMakeLists.txt              (root build file)
├── cmake/                      (CMake helper modules)
├── include/enigmadb/           (public headers the API surface)
│   ├── common/                 (Result type, error codes, types, platform detection)
│   ├── io/                     (I/O abstraction, POSIX / io_uring layer)
│   ├── storage/                (storage engine public interface)
│   ├── <model>/                (wide column model)
│   ├── query/                  (query engine)
│   └── server/                 (networking)
├── src/                        (implementation files, mirrors include/ structure)
│   ├── common/
│   ├── io/
│   ├── storage/
│   ├── document/
│   ├── query/
│   └── server/
├── tests/                      (mirrors src/ structure)
│   ├── common/
│   ├── io/
│   ├── storage/
│   └── ...
├── bench/                      (microbenchmarks)
├── tools/                      (CLI shell, utilities)
└── third_party/                (vendored dependencies)
```

## Platform

Note that this *WILL NOT* compile on Windows machines.

- [x] MacOS (Apple Silicon)
- [ ] Linux (To be tested, Trixie)
