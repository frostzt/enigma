# enigma

Engine and idea inpired by Rocks, the name is inspired by my favorite Soft Support/Offlaner
Enigma in Dota 2.

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

## Benchmarking

```shell
cmake -B build -DENIGMADB_BUILD_BENCHMARKS=ON
cmake --build build --target benchmark_enigma -j
./build/benchmark/benchmark_enigma
```

## Debugging and tols

Have created a bunch of tools to help debug and understand stuff inside tools dir

```shell
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENIGMADB_BUILD_TOOLS=ON
cmake --build build --target sstable_dump
./build/tools/sstable_dump path/to/sst_00000001.db
```

## Platform

Note that this *WILL NOT* compile on Windows machines.

- [x] MacOS (Apple Silicon)
- [x] Linux (Trixie, partial NOT under ASan)

## Todo's and Fixme's

- [x] Doesn't compile in `Debug` under ASan in Linux
- [ ] Most of system is missing logging
