Here's a full production-grade logger design — the kind you build once and grow into. I've organized it as an architecture, then component-by-component, then a build-order so you can implement it in usable slices rather than all at once.

## 1. The pipeline (mental model)

```
LOG_INFO(cat, "…", args)                       [call site, any thread]
   │  macro: level guard + capture file/line/func
   ▼
log_impl<Args>(...)  → std::format on caller thread → LogRecord
   │  (format on the CALLER thread — critical, see §5)
   ▼
Logger::dispatch(record)
   ├─► fan-out to N sinks ──► ConsoleSink / FileSink        [sync path]
   ├─► RingBufferSink (always on, in-memory black box)      [§7]
   └─► AsyncSink → MPSC queue → worker thread → real sinks  [§6]
                                                   │
crash (SIGSEGV/ABRT) ──► signal handler ──► dump RingBuffer via write(2)  [§8]
```

Two ideas make it production-grade: **format on the calling thread** (never defer raw args), and **sinks are composable** (console, file, ring, async are all the same interface, fanned out).

## 2. Core types

```cpp
enum class Level { Trace, Debug, Info, Warn, Error, Fatal, Off };  // ordered
enum class Category { General, Wal, Sstable, Memtable, Compaction, Io, _Count };

struct LogRecord {
    Level        level;
    Category     category;
    std::chrono::system_clock::time_point ts;
    uint64_t     tid;              // thread id
    const char*  file;            // __FILE__ (static storage, cheap to copy)
    int          line;
    const char*  func;
    std::string  message;         // already formatted
};
```

`LogRecord` is the unit that flows through the system. `file`/`func` are `const char*` into static string literals — no copies.

## 3. Front-end (macros + templated formatter)

Same shape we settled on, unchanged by the bigger design:

```cpp
#define ENIGMA_LOG(cat, lvl, ...)                                    \
    do {                                                             \
        if (::enigmadb::log_enabled((cat), (lvl)))           \
            ::enigmadb::log_impl((cat),(lvl),                \
                __FILE__,__LINE__,__func__, __VA_ARGS__);            \
    } while (0)
```

- `log_enabled` = inlined relaxed atomic load of the per-category level; mark the disabled path `[[unlikely]]` so TRACE/DEBUG cost ~nothing in prod.
- `LOG_TRACE`/`LOG_DEBUG` compile to `((void)0)` under `NDEBUG`.
- Compile-time **minimum** level too: a `constexpr Level kCompileMinLevel` so you can hard-strip levels below it at build time regardless of runtime config.

`log_impl` formats via `std::vformat(fmt.get(), std::make_format_args(args...))` (named args = lvalues, avoids the `make_format_args` rvalue gotcha), builds a `LogRecord`, and calls `Logger::instance().dispatch(std::move(rec))`.

## 4. Sinks (the composability layer)

```cpp
struct LogSink {
    virtual ~LogSink() = default;
    virtual void submit(const LogRecord&) = 0;   // format + emit
    virtual void flush() = 0;
    void set_formatter(...); void set_level(Level);  // per-sink threshold
};
```

Concrete sinks (each is small, independent):
- **ConsoleSink** — stderr, optional ANSI color by level (TTY-detected).
- **FileSink** — append to a file; `flush()` policy (every N records or every T ms).
- **RotatingFileSink** — size/time-based rotation (later).
- **RingBufferSink** — §7.
- **AsyncSink** — a *wrapper* sink that owns the queue + worker and forwards to a set of "real" sinks off-thread (§6).

The `Logger` holds `std::vector<std::shared_ptr<LogSink>>` and fans out. Per-sink levels let you e.g. put everything in the ring buffer but only `Info+` on the console.

**4. §8 Crash handler + §9 lifecycle** — reconcile with the existing panic facility instead of duplicating it:
- `server_panic`/`server_assert` should route through the logger's `LOG_FATAL` path (flush sinks + dump ring buffer, then abort), folding in `_server_panic_impl`.
- Flag that `error.cpp:21`'s `backtrace_symbols` **allocates** and must become `backtrace_symbols_fd` in any signal-handler context (§8 already prescribes `_fd`; I'll add an explicit "migrate the existing impl" note).

## 5. Why format on the caller thread

The tempting optimization is to enqueue *raw args* and format on the background thread. **Don't** — it's the classic dangling-reference footgun: you log `key` (a `std::vector<uint8_t>&`), the caller mutates/frees it, and the worker formats garbage. spdlog formats on the caller precisely to avoid this. The caller pays the `std::format` cost (cheap, CPU-only); only the slow **I/O** is deferred. This keeps lifetimes trivially correct.

## 6. Async subsystem

- **Queue**: bounded MPSC. v1 = `std::mutex` + `std::condition_variable` + ring of `LogRecord` (spdlog's blocking-queue design). Lock-free is a later swap behind the same interface.
- **Worker**: single consumer thread; pops in batches, calls `submit` on each downstream sink, periodic `flush` (e.g. every 250 ms or N records), wakes on a flush request.
- **Backpressure policy** when the queue is full (this is a real decision — see questions):
  - `Block` — producer waits (no loss, can stall the hot path).
  - `DropNewest` / `DropOldest` — never stall, drops with a counter you can surface.
  - Common DB choice: **block for `>=Warn`, drop for `<Warn`** so you never lose important lines but never stall on debug spam.
- **Shutdown**: explicit `Logger::shutdown()` — stop accepting, drain queue, flush all sinks, join worker. Must run before static destructors (see §9).

## 7. Ring buffer (the black box)

Design for the thing that actually matters: **being dumpable from a crash handler.**

- Fixed `N` slots (e.g. 1024), each a **fixed-size preformatted byte buffer** (e.g. 512 bytes) — *not* a `std::string`, *not* structured. Preformat the whole line at insert time.
- `std::atomic<uint64_t> write_seq` advanced per record; slot = `seq % N`; overwrites oldest on wrap.
- Why preformatted + fixed buffers: the crash dumper must be **async-signal-safe** — no malloc, no `std::format`, no locks. So at insert time (in normal context) you format into the slot; at dump time (in the handler) you only `write(2)` raw bytes.
- It's modeled as just another sink (`RingBufferSink`) in the fan-out, always enabled at `Trace` so it captures maximum history regardless of what console/file show.
- Tearing: a reader in the handler may catch a slot mid-write. Accept it (best-effort forensics) or use a per-slot `ready` flag / seqlock. For v1, best-effort + a "possibly torn last line" caveat is fine.

## 8. Crash handler

- Install handlers for `SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGFPE`, `SIGILL` (use `sigaction`, `SA_SIGINFO`).
- In-handler, **async-signal-safe only**: dump the ring buffer with `write(2)` to `STDERR_FILENO` (and/or a preopened crash-log fd), then optionally a backtrace via `backtrace()` + `backtrace_symbols_fd` (the `_fd` variant avoids malloc; plain `backtrace_symbols` allocates — avoid it).
- Re-raise the default handler afterward so you still get a core dump. This is exactly Valkey's `serverLogFromHandler` philosophy.
- `LOG_FATAL` (non-signal) path: flush sinks, dump ring buffer normally, then `std::abort()` (clean stack under your ASan/UBSan build).

## 9. Lifecycle & the static-init-order trap

- Global logger via a **Meyers singleton** (`static Logger& instance()` with a function-local static) — constructed on first use, dodges static-init-order fiasco.
- `init(const LogConfig&)` sets sinks/levels; safe defaults (console sink, `Info`) so logging works even before explicit init.
- Register `shutdown()` with `std::atexit`, and guard against double-shutdown. The subtle bit: the async worker must be joined before any sink is destroyed — so ownership order and an explicit shutdown are essential (don't rely purely on destructor order).

## 10. Configuration

```cpp
struct LogConfig {
    Level default_level = Level::Info;
    std::array<Level, Category::_Count> category_levels;  // per-subsystem
    bool async = false;
    size_t queue_capacity = 8192;
    OverflowPolicy overflow = OverflowPolicy::BlockOnWarnPlus;
    std::optional<std::string> file_path;
    bool console = true;
    bool enable_crash_handler = false;
    size_t ring_slots = 1024, ring_slot_bytes = 512;
};
```
Env overrides: `ENIGMADB_LOG_LEVEL=debug`, `ENIGMADB_LOG=wal:trace,compaction:info`, `ENIGMADB_LOG_FILE=…`, `ENIGMADB_LOG_ASYNC=1`.

## 11. Performance techniques (the ones that matter)

- Inlined atomic level check, disabled path `[[unlikely]]`.
- **Thread-local reusable format buffer** so `log_impl` doesn't allocate per call (reuse a `std::string` you `clear()`).
- **Cached timestamp**: format the `YYYY-MM-DD HH:MM:SS` part once per second (atomic-guarded), only render milliseconds per record — spdlog's trick; the biggest single win.
- Batch drains in the worker; never flush per-record on the hot path.

## 12. Extras worth designing in (cheap to add later behind the same API)

- **Rate limiting**: `LOG_EVERY_N(n, ...)`, `LOG_FIRST_N(n, ...)` using a static atomic counter at the call site — essential for hot loops like compaction merges.
- **Structured fields**: an overload that takes key-values for future JSON output; skip formatting them into the message.

## 13. File layout & build

-  `include/enigmadb/log.h`,  `include/enigmadb/log_sinks.h`
-  `src/log.cpp`,  `src/log_sinks.cpp`,  `src/log_async.cpp`,  `src/log_crash.cpp`
- (drop the non-existent `common/` subdir)

No CMake changes needed (`GLOB_RECURSE CONFIGURE_DEPENDS` + `PUBLIC` include dir already cover it). Suggested split for clarity:
-  `include/enigmadb/common/log.h` — levels, categories, macros, `log_impl`, `log_enabled`, config, init/shutdown.
-  `include/enigmadb/common/log_sinks.h` — `LogSink` + concrete sink decls.
-  `src/common/log.cpp` — `Logger` singleton, dispatch, config/env, level state.
-  `src/common/log_sinks.cpp` — console/file/ring sink impls.
- ` src/common/log_async.cpp` — queue + worker.
-  `src/common/log_crash.cpp` — signal handlers + signal-safe ring dump.

## 14. Suggested build order (each phase is usable on its own)

1. **Core sync**: levels, categories, macros, `log_impl` split, ConsoleSink. → replaces your `std::cout` sites immediately.
2. **Config + FileSink**: env parsing, per-category levels, multi-sink fan-out.
3. **RingBufferSink**: in-memory history + an on-demand (non-signal) dump.
4. **AsyncSink**: queue + worker + backpressure + shutdown ordering.
5. **Crash handler**: signal-safe ring dump + backtrace, `LOG_FATAL` path.
6. **Polish**: rate limiting, timestamp caching, color, rotation.

## 15. Testing strategy

- **Injectable clock** (a `now()` functor) → deterministic timestamps in tests.
- **TestSink** capturing records → assert format, gating, per-category levels.
- Async: assert all records drain, and backpressure policy behaves (block vs drop counter).
- Ring buffer: fill past capacity, assert wraparound keeps the newest N; simulate a dump and check contents.

**5. §14 Phase 1 (lines 159-164)** — name the concrete `std::cout`/`std::cerr` cruft to replace first:
- ` include/enigmadb/storage/dazzle_db/sstable/sstable_common.h:84,87`
-  `src/storage/dazzle_db/dazzle_engine.cpp:264`
- plus: migrate `server_panic` in  `error.cpp` onto `LOG_FATAL` during Phase 5 (crash handler).

**6. §2 Categories (line 26)** — add a one-line note that `Wal/Sstable/Memtable/Compaction` are `dazzle`-engine-internal and future engines may want their own, so the enum may need namespacing/extension (no forced change).

