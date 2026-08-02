# EnigmaDB — Exhaustive Test Plan

Generated this by using Greptile and plenty of tests misssing need to handle all those.

---

## Test categories used below

| Tag | Meaning |
|---|---|
| **[UNIT]** | Pure logic, no I/O, microseconds |
| **[IO]** | Touches the filesystem |
| **[CRASH]** | Simulates a crash at a specific point, then recovers |
| **[FUZZ]** | Randomized input, property-checked |
| **[PROP]** | Property-based — an invariant that must hold for all inputs |
| **[PERF]** | Guards against complexity regressions, not absolute speed |
| **[ASAN]** | Must run under AddressSanitizer specifically |

---

## Cross-cutting rules

- [ ] **TEST-000:** Assert on `error.code`, never `error.message`. Message text is presentation. Audit the existing suite — at least one test already broke on this.
- [ ] **TEST-001:** Every loop-based test needs a post-loop count assertion. A per-iteration loop structurally cannot catch "stopped early."
- [ ] **TEST-002:** All numeric test keys zero-padded so lexicographic order matches numeric order. `user:10000 < user:1001 < user:11` otherwise.
- [ ] **TEST-003:** Every test that constructs data with sequence numbers must **scramble** them relative to key order, so no assertion can pass by accidental alignment.
- [ ] **TEST-004:** Whole suite runs clean under ASan and UBSan. Add a CI job.
- [ ] **TEST-005:** `grep -r catalog include/enigmadb/storage src/storage` returns zero hits — make it a CI step, not a habit.
- [ ] **TEST-006:** Every module gets a "destructor releases resources" test — open N handles, destroy, assert fd count returns to baseline (`/proc/self/fd` on Linux).

---

## Module 1 — `common/` (Result, Error, encoding, CRC32, HLC, BloomFilter)

### 1.1 Result / ExpectResult
- [ ] **TEST-010** [UNIT] Move-only payload: construct with a move-only type, move through several `Result`s, assert no copies (use a counting type).
- [ ] **TEST-011** [UNIT] `Result<void>` ok and err paths.
- [ ] **TEST-012** [UNIT] Accessing `.value()` on an err — assert it fails loudly (throw/assert), not UB.
- [ ] **TEST-013** [UNIT] Accessing `.error()` on an ok — same.
- [ ] **TEST-014** [UNIT] Nested `Result<Result<T>>` doesn't collapse or lose the inner error.
- [ ] **TEST-015** [UNIT] `Result` holding a type with a throwing constructor — no leak, no partially-formed state.

### 1.2 Integer encoding
- [ ] **TEST-020** [UNIT] Roundtrip every width at 0, 1, max, max-1, and a value with alternating bits (`0xAA55...`).
- [ ] **TEST-021** [UNIT] **Byte order is explicitly big-endian** — encode a known value, assert the exact byte sequence. Existing roundtrip tests pass under either endianness; they don't pin the format.
- [ ] **TEST-022** [UNIT] Encoding at a non-zero offset into a buffer doesn't disturb neighbouring bytes (write sentinel bytes around the target region).
- [ ] **TEST-023** [UNIT] Returned offset always equals input offset + width.
- [ ] **TEST-024** [FUZZ] Random values, all widths, roundtrip — 10k iterations.

### 1.3 CRC32
- [ ] **TEST-030** [UNIT] Known-answer vectors against a reference implementation. Roundtrip tests prove self-consistency, not correctness — a wrong-but-deterministic CRC passes them.
- [ ] **TEST-031** [UNIT] Empty buffer.
- [ ] **TEST-032** [UNIT] Single-bit flip at every position of a 64-byte buffer changes the checksum — all 512 positions.
- [ ] **TEST-033** [UNIT] Byte-swap of two adjacent bytes changes the checksum (catches order-insensitive hashes).
- [ ] **TEST-034** [PERF] Large buffer (1MB) completes in reasonable time — guards against an accidental O(n²).

### 1.4 HLC / TimestampGenerator
- [ ] **TEST-040** [UNIT] Strictly monotonic across N sequential calls.
- [ ] **TEST-041** [UNIT] Two calls within the same physical tick still produce distinct, increasing values (the logical counter does its job).
- [ ] **TEST-042** [UNIT] Physical clock going *backwards* doesn't produce a decreasing timestamp — inject a mock clock.
- [ ] **TEST-043** [UNIT] Logical counter overflow behaviour is defined.
- [ ] **TEST-044** [ASAN] Concurrent generation from N threads — all values distinct, all monotonic per-thread. (One exists; extend to assert global distinctness.)

### 1.5 BloomFilter
- [ ] **TEST-050** [PROP] **No false negatives, ever.** Insert 10k keys, assert `may_contain` is true for every one. This is the only correctness guarantee a bloom filter makes.
- [ ] **TEST-051** [PROP] Measured false-positive rate is within tolerance of the configured rate — insert 10k, probe 100k absent keys, assert observed FP rate ≤ 2× configured.
- [ ] **TEST-052** [UNIT] Serialize → deserialize → same answers for the same probe set.
- [ ] **TEST-053** [UNIT] Zero expected keys, and one expected key — no division-by-zero or zero-sized bit array.
- [ ] **TEST-054** [UNIT] Massively over-inserting (10× expected) degrades the FP rate but never produces a false negative.
- [ ] **TEST-055** [UNIT] Two filters with different `num_hashes` don't accidentally interoperate.
- [ ] **TEST-056** [FUZZ] Random key sets, random configs, assert TEST-050's property holds throughout.

---

## Module 2 — `io/` (IOEngine, PosixIOEngine, FileHandle)

- [ ] **TEST-060** [IO] Short write: `append` a buffer larger than the pipe/socket buffer, assert the retry loop writes everything.
- [ ] **TEST-061** [IO] `read` at exactly EOF returns 0 bytes, not an error.
- [ ] **TEST-062** [IO] `read` spanning EOF returns a partial count.
- [ ] **TEST-063** [IO] `read` at an offset beyond EOF.
- [ ] **TEST-064** [IO] Zero-length read and zero-length append are both no-ops, not errors.
- [ ] **TEST-065** [ASAN] `FileHandle` closes its fd in the destructor — open/destroy in a loop 10k times, assert no fd exhaustion.
- [ ] **TEST-066** [ASAN] `FileHandle` is move-only and the moved-from handle does **not** close the fd (double-close is silent corruption on a reused fd).
- [ ] **TEST-067** [IO] `sync_directory` on a path that isn't a directory returns an error.
- [ ] **TEST-068** [IO] Open with `Mode::Write` on an existing file — document and assert whether it truncates or appends.
- [ ] **TEST-069** [IO] Permission-denied path returns an error rather than crashing.
- [ ] **TEST-070** [IO] `file_size` on an empty file, and after each of several appends.
- [ ] **TEST-071** [IO] Two handles to the same file — writes through one are visible to the other after sync.

---

## Module 3 — `catalog/key_encoding` ⚠️ HIGHEST PRIORITY

The entire model-agnostic-storage claim rests on this module. Currently **untested**.

### 3.1 Ordering — the load-bearing property
- [ ] **TEST-080** [PROP] ⭐ **Encoded order == logical order.** Build a list of (partition, clustering, column) triples, encode each, sort the blobs with plain `std::lexicographical_compare`, assert the result matches the expected component-wise order. **This single test justifies `Key::operator<` being a one-liner.**
- [ ] **TEST-081** [UNIT] Same partition, different clustering → ordered by clustering.
- [ ] **TEST-082** [UNIT] Same partition + clustering, different column → ordered by column.
- [ ] **TEST-083** [UNIT] Different partition → partition decides, regardless of what clustering/column contain. Specifically: `("ab", "zzz", "zzz")` sorts before `("abc", "aaa", "aaa")`.
- [ ] **TEST-084** [UNIT] Prefix relationships: `"ab"` before `"abc"`; `""` before everything.
- [ ] **TEST-085** [UNIT] Components containing `0x00` sort correctly relative to components that don't — specifically that an escaped zero (`0x00 0xFF`) sorts **above** a terminator (`0x00 0x01`).
- [ ] **TEST-086** [UNIT] Component containing `0xFF`, and one containing `0x01`, adjacent to a boundary.
- [ ] **TEST-087** [FUZZ] ⭐ 10k random triples: encode all, sort encoded, sort logically, assert the two orderings are identical. This is the test most likely to find an encoding bug you didn't think of.

### 3.2 Roundtrip
- [ ] **TEST-090** [UNIT] Roundtrip with ordinary ASCII in all three components.
- [ ] **TEST-091** [UNIT] Roundtrip with embedded `0x00` in each component independently, and in all three at once.
- [ ] **TEST-092** [UNIT] Roundtrip with `0x00 0x00 0x00` (consecutive zeros — catches escape-state bugs).
- [ ] **TEST-093** [UNIT] Roundtrip with all three components empty.
- [ ] **TEST-094** [UNIT] Roundtrip with a component that is exactly `0x00 0x01` as literal data (the terminator sequence appearing as content).
- [ ] **TEST-095** [UNIT] Roundtrip with a component that is exactly `0x00 0xFF` as literal data.
- [ ] **TEST-096** [UNIT] Roundtrip with a 1MB component.
- [ ] **TEST-097** [FUZZ] 10k random byte sequences including high-zero-density inputs → roundtrip.

### 3.3 Malformed input (decoder hardening)
Bytes come off disk and can be corrupt. Each of these must return an error, never UB.
- [ ] **TEST-100** [UNIT] Empty input.
- [ ] **TEST-101** [UNIT] Truncated mid-component (no terminator before end).
- [ ] **TEST-102** [UNIT] Trailing lone `0x00` at the very end.
- [ ] **TEST-103** [UNIT] `0x00` followed by a byte that is neither `0x01` nor `0xFF`.
- [ ] **TEST-104** [UNIT] Only one component terminated, then end of input.
- [ ] **TEST-105** [UNIT] Only two components terminated.
- [ ] **TEST-106** [UNIT] Four terminators present (extra trailing data) — define and assert the behaviour.
- [ ] **TEST-107** [FUZZ] ⭐ Feed 100k random byte buffers to the decoder. **Never crash, never hang, never read out of bounds.** Run under ASan. Every input either decodes or returns an error.
- [ ] **TEST-108** [FUZZ] Take valid encodings and corrupt one random byte — assert error or valid-but-different, never a crash.

---

## Module 4 — `storage/key.h`, `value.h`

- [ ] **TEST-110** [UNIT] `Key` default-constructs to empty and is usable as a map key.
- [ ] **TEST-111** [UNIT] `operator<` is a **strict weak ordering** — verify irreflexivity, asymmetry, and transitivity over a set of keys. A broken comparator here corrupts `std::map` silently.
- [ ] **TEST-112** [UNIT] `operator==` consistent with `operator<=>` (neither `a<b` nor `b<a` ⟺ `a==b`).
- [ ] **TEST-113** [UNIT] Empty key compares less than every non-empty key.
- [ ] **TEST-114** [UNIT] Keys differing only in length (prefix) order correctly.
- [ ] **TEST-115** [UNIT] `Key` with bytes above `0x7F` compares by **unsigned** value — catches a signed-char bug that would order `0x80` before `0x01`.
- [ ] **TEST-116** [UNIT] `bytes()` returns a view that can't be used to mutate the key.
- [ ] **TEST-117** [UNIT] Move semantics: moved-from `Key` is valid and empty.
- [ ] **TEST-118** [FUZZ] Random keys sorted by `operator<` vs sorted by `memcmp` — identical order.

---

## Module 5 — `dazzle_db/memtable`

- [ ] **TEST-130** [UNIT] Byte accounting: put a key, assert `approximate_size()`; overwrite with a **larger** value, assert the delta; overwrite with a **smaller** value, assert the delta; delete, assert the delta. Hand-compute every expectation.
- [ ] **TEST-131** [UNIT] ⭐ Overwrite the same key 100× with varying value sizes — `approximate_size()` must never underflow to a huge number and `count()` stays 1. This is the unsigned-underflow bug that was live for weeks.
- [ ] **TEST-132** [UNIT] Delete a key whose value was much larger than the key — the classic underflow trigger.
- [ ] **TEST-133** [UNIT] Delete a key that was never inserted — counts as a new key, tombstone stored, size increases.
- [ ] **TEST-134** [UNIT] Delete then re-insert then delete — sequence and tombstone state correct at each step.
- [ ] **TEST-135** [UNIT] Iteration order is ascending by `Key` for a scrambled insertion order.
- [ ] **TEST-136** [UNIT] `should_flush()` is false below threshold and true at/above it — test exactly at the boundary.
- [ ] **TEST-137** [UNIT] Sequence stored is exactly what was passed, for both put and remove.
- [ ] **TEST-138** [UNIT] Empty memtable: `count()==0`, `approximate_size()==0`, `begin()==end()`, `get()` returns nullopt.
- [ ] **TEST-139** [UNIT] Zero-length value stored and retrieved as distinct from a tombstone.
- [ ] **TEST-140** [PERF] 100k inserts — no quadratic blowup.

---

## Module 6 — `dazzle_db/wal`

### 6.1 Record serialization
- [ ] **TEST-150** [UNIT] Roundtrip PUT with ordinary key/value.
- [ ] **TEST-151** [UNIT] Roundtrip DELETE (zero-length value present, not absent).
- [ ] **TEST-152** [UNIT] Roundtrip with empty key and empty value.
- [ ] **TEST-153** [UNIT] Roundtrip with a 1MB value.
- [ ] **TEST-154** [UNIT] Roundtrip with `0x00`-heavy key and value.
- [ ] **TEST-155** [UNIT] **Exact byte layout** — serialize a known record, assert the resulting bytes match a hardcoded expected array. Pins the on-disk format; roundtrip tests do not.
- [ ] **TEST-156** [UNIT] CRC covers the whole body: flip a bit in each field in turn, assert corruption is detected every time.
- [ ] **TEST-157** [UNIT] Truncated buffer at every length from 0 to full record — error, never a crash. Assert on `error.code`.
- [ ] **TEST-158** [UNIT] Length field claiming more than the buffer holds.
- [ ] **TEST-159** [UNIT] Length field claiming zero.
- [ ] **TEST-160** [FUZZ] ⭐ 100k random buffers into the deserializer under ASan — never crash, never OOM-allocate from a bogus length field.

### 6.2 Writer / Reader
- [ ] **TEST-170** [IO] Write N records, read back N records, all fields equal.
- [ ] **TEST-171** [IO] Reader stops cleanly at end of file.
- [ ] **TEST-172** [CRASH] ⭐ **Torn write:** truncate the WAL mid-record, assert the reader replays all *complete* records and stops without error. This is the normal post-crash state and must not be fatal.
- [ ] **TEST-173** [CRASH] Truncate at every byte offset in a multi-record WAL — replay always yields a prefix of the records, never garbage, never a crash.
- [ ] **TEST-174** [CRASH] Corrupt a byte in the middle record of three — replay stops at the corruption; records before it survive.
- [ ] **TEST-175** [IO] Empty WAL file — reader yields nothing, no error.
- [ ] **TEST-176** [IO] WAL containing only a partial header.
- [ ] **TEST-177** [IO] `sync()` is actually called before `append` returns to the caller (durability contract) — verify with a mock IOEngine counting sync calls.

---

## Module 7 — `dazzle_db/sstable`

### 7.1 Writer
- [ ] **TEST-190** [IO] Entries spanning exactly one block boundary — the entry that triggers the flush lands in the new block, not split across two.
- [ ] **TEST-191** [IO] A single entry larger than the block target — must still be written (block size is a target, not a hard cap).
- [ ] **TEST-192** [IO] Empty SSTable: finish with no entries, footer valid, reader opens it, iterator immediately invalid.
- [ ] **TEST-193** [IO] Exactly one entry.
- [ ] **TEST-194** [IO] Index has exactly one entry per data block — count blocks, count index entries, assert equal.
- [ ] **TEST-195** [IO] `first_key` of each index entry equals the actual first key of that block.
- [ ] **TEST-196** [IO] `max_sequence` in the footer equals the true max over all entries — including when the max is **not** the last entry written.
- [ ] **TEST-197** [IO] Footer checksum covers all footer fields — flip a bit in each, assert detection.
- [ ] **TEST-198** [IO] Magic number present at the expected offset.
- [ ] **TEST-199** [IO] Writing unsorted entries — **document and assert** the behaviour (error? UB? silently broken index?). Currently an unenforced precondition.

### 7.2 Reader
- [ ] **TEST-210** [IO] `get()` for every key that was written — all found, correct values and sequences.
- [ ] **TEST-211** [IO] `get()` for keys that sort before the first, after the last, and between two existing keys — all not-found.
- [ ] **TEST-212** [IO] `get()` for the exact first and last key in each block (boundary conditions in the binary search).
- [ ] **TEST-213** [IO] Tombstone entries are returned with `is_tombstone == true`, not filtered.
- [ ] **TEST-214** [IO] Bloom filter false positive path: force a probe that passes the filter but misses in the block — must return not-found cleanly.
- [ ] **TEST-215** [CRASH] Truncated SSTable file — `create()` returns an error, doesn't crash.
- [ ] **TEST-216** [CRASH] Footer intact but index block corrupt.
- [ ] **TEST-217** [CRASH] Wrong magic → error.
- [ ] **TEST-218** [CRASH] Wrong format version → error mentioning version mismatch.
- [ ] **TEST-219** [CRASH] Bit-flip fuzz: corrupt one random byte in a valid SSTable, 1000 iterations, under ASan — every outcome is either a clean error or correct data, never a crash or an out-of-bounds read.
- [ ] **TEST-220** [IO] The in-block early-exit (`ord < 0` → break) — verify a key that would sort mid-block returns not-found without scanning the rest.

### 7.3 Iterator
- [ ] **TEST-230** [IO] Iterate all entries across many blocks — order, keys, values, sequences, tombstone flags all correct, plus a final count assertion.
- [ ] **TEST-231** [IO] Empty SSTable → invalid immediately after `seek_to_first()`.
- [ ] **TEST-232** [IO] Single entry → valid once, then invalid.
- [ ] **TEST-233** [IO] `seek_to_first()` twice — second call restarts cleanly from the beginning.
- [ ] **TEST-234** [IO] `next()` past exhaustion repeatedly — stays invalid, doesn't crash, doesn't wrap.
- [ ] **TEST-235** [IO] `status()` is ok throughout a clean iteration.
- [ ] **TEST-236** [CRASH] Iterator over a corrupted block — becomes invalid **and** `status()` reports the error (not silently "exhausted").
- [ ] **TEST-237** [IO] Block-boundary crossing: entries positioned so that a block ends exactly at a block-size boundary.

---

## Module 8 — `dazzle_db/merge_iterator`

Existing coverage is decent. These are the gaps.

- [ ] **TEST-250** [UNIT] Zero sources → invalid immediately.
- [ ] **TEST-251** [UNIT] All sources empty → invalid immediately.
- [ ] **TEST-252** [UNIT] Single source → passthrough, identical output.
- [ ] **TEST-253** [UNIT] ⭐ **Error propagation:** a source whose `status()` goes bad mid-iteration. The merge must become invalid **and** surface the error. Requires the `fail_after_n` flag on `FakeInternalIterator`. Currently **untested** despite a real bug having been fixed here.
- [ ] **TEST-254** [UNIT] Error on the very first `seek_to_first()` of one source.
- [ ] **TEST-255** [UNIT] Same key in **all** sources at identical sequences — defined, deterministic winner (document which).
- [ ] **TEST-256** [UNIT] Key present in 10 sources at 10 different sequences — highest wins, other 9 fully consumed.
- [ ] **TEST-257** [UNIT] Two distinct keys where one is a byte-prefix of the other, interleaved across sources.
- [ ] **TEST-258** [UNIT] `seek_to_first()` called twice — full restart, no leaked state from the first pass.
- [ ] **TEST-259** [UNIT] 100 sources — heap correctness at scale.
- [ ] **TEST-260** [PROP] ⭐ Generate K random sorted sources with overlapping keys; assert output is (a) sorted, (b) each key appears exactly once, (c) each key's value is the one with the highest sequence among all sources holding it. Cross-check against a `std::map` built by inserting all entries in sequence order.
- [ ] **TEST-261** [PERF] N total entries across K sources completes in O(N log K) — time 10× the data, assert time doesn't grow 100×.
- [ ] **TEST-262** [ASAN] Source destroyed while the merge still holds a pointer — should be caught by ASan. Documents the lifetime contract.

---

## Module 9 — `dazzle_db/compaction`

- [x] **TEST-270** [UNIT] ⭐ `can_drop_tombstones`: full input set → true.
- [x] **TEST-271** [UNIT] ⭐ Contiguous run from the oldest → true.
- [x] **TEST-272** [UNIT] ⭐ **Gap in the middle** (inputs {1,3}, live {1,2,3}) → **false**. The resurrection counterexample. If this returns true, deletes come back from the dead.
- [x] **TEST-273** [UNIT] Newest-only subset → false.
- [x] **TEST-274** [UNIT] Empty inputs → false (or error).
- [ ] **TEST-275** [UNIT] Single live table, compacting it → true.
- [ ] **TEST-276** [IO] ⭐ **Resurrection end-to-end:** write `x=5` (SST-1), `x=99` (SST-2), delete `x` (SST-3). Compact {1,3} only. Assert `get(x)` is **still not-found**. This is the bug the predicate exists to prevent, tested through the real stack.
- [ ] **TEST-277** [IO] Full compaction drops tombstones — inspect the output SSTable directly, assert zero tombstone entries.
- [ ] **TEST-278** [IO] Partial compaction preserves tombstones — inspect output, assert tombstone count equals delete count.
- [ ] **TEST-279** [IO] Subset compaction: non-compacted tables survive on disk and their data is still readable.
- [ ] **TEST-280** [IO] Compaction output entry count == unique live keys across all inputs.
- [ ] **TEST-281** [IO] Every surviving key's value is the newest version.
- [ ] **TEST-282** [CRASH] ⭐ Crash **after** the new SSTable is durable but **before** old files are deleted → reopen, assert all data readable and correct (duplicates resolved by sequence).
- [ ] **TEST-283** [CRASH] Crash after deleting some but not all old files → reopen, data intact.
- [ ] **TEST-284** [IO] Compaction with one input file unreadable → clean error, engine state unchanged, old readers still valid.
- [ ] **TEST-285** [IO] Compaction where the output SSTable can't be created (disk full / permission) → error, no state mutation.
- [ ] **TEST-286** [ASAN] ⭐ Reader lifetime: after compaction, assert **no** reads are served from unlinked-but-open files. Write a canary value only present in an old table; after compaction it must be unreachable.
- [ ] **TEST-287** [IO] Compact twice in a row — second compaction over a single table is a no-op or clean.
- [ ] **TEST-288** [IO] Compacting SSTables with zero overlapping keys.
- [ ] **TEST-289** [IO] Compacting SSTables with 100% overlapping keys.

---

## Module 10 — `dazzle_db/dazzle_engine` (integration)

### 10.1 Read path
- [ ] **TEST-300** [IO] ⭐ **Overwrite across flushes:** put K=v1, flush, put K=v2, flush, `get(K)` → v2. Catches a wrong `sst_readers_` iteration direction. **Nothing currently covers this.**
- [ ] **TEST-301** [IO] Same, three flushes deep.
- [ ] **TEST-302** [IO] Value in memtable shadows all SSTables.
- [ ] **TEST-303** [IO] Tombstone in memtable shadows a live value in an SSTable.
- [ ] **TEST-304** [IO] Tombstone in a newer SSTable shadows a live value in an older one.
- [ ] **TEST-305** [IO] Live value in a newer SSTable overrides a tombstone in an older one (resurrection after re-insert).
- [ ] **TEST-306** [IO] `get()` on a never-written key with many SSTables present → not-found, and confirm bloom filters short-circuit most reads.

### 10.2 Sequence numbers
- [ ] **TEST-310** [UNIT] ⭐ Sequences strictly increase across puts, deletes, flushes, and compactions — no repeats, no decreases. Assert over a 1000-operation mixed workload.
- [ ] **TEST-311** [CRASH] ⭐ Reopen after crash — the next sequence issued exceeds every sequence ever durably written. Verify against both SSTable footers and WAL contents.
- [ ] **TEST-312** [CRASH] Replayed entries retain their **original** sequences (not re-minted).
- [ ] **TEST-313** [CRASH] ⭐ Replayed **tombstones** retain their original sequences. Still missing.
- [ ] **TEST-314** [CRASH] Open → close → open → close, 10 cycles with no writes: sequence doesn't drift upward. Pins the double-`+1` bug.
- [ ] **TEST-315** [UNIT] `latest_lsn()` semantics documented and asserted — "next to assign" vs "highest assigned," consistently.

### 10.3 Recovery
- [ ] **TEST-318** [IO] ⭐ **WAL rotation across flushes.** open → write → flush → write → flush → write. Assert at each step: exactly one active WAL exists, its id is what you expect, the previous segment was deleted, and **the active WAL is never the one deleted**. This is the test that would have caught the counter-semantics bug where `recover()` treated the live WAL as an old segment.
- [ ] **TEST-319** [CRASH] Write → flush → write more → crash → reopen. The post-flush writes live in the *rotated* WAL; assert they recover. Existing recovery tests write-then-drop without an intervening flush, so this path is uncovered.
- [ ] **TEST-320** [CRASH] Crash with data only in the memtable → all recovered.
- [ ] **TEST-321** [CRASH] Crash with some data flushed, some in WAL → all recovered, correct versions.
- [ ] **TEST-322** [CRASH] Crash during flush, after the SSTable is written but before the WAL is deleted → no data loss, no duplicates after recovery.
- [ ] **TEST-323** [CRASH] Crash with multiple un-replayed WAL segments → replayed in sequence order.
- [ ] **TEST-324** [CRASH] WAL segment with a torn final record → complete records recovered, torn one dropped.
- [ ] **TEST-325** [CRASH] Empty data directory → clean bootstrap.
- [ ] **TEST-326** [CRASH] Data directory with SSTables but no WAL → opens, data readable.
- [ ] **TEST-327** [CRASH] Data directory with a stray non-`.db` file in `sst/` → ignored, not parsed as an SSTable.
- [ ] **TEST-328** [CRASH] SSTable file with a malformed name (`sst_notanumber.db`) → clean error, not an exception from `stoll`.
- [ ] **TEST-329** [CRASH] ⭐ **Crash-only equivalence:** identical operations, one run ending with a clean drop and one with `_exit(1)`. Both recover to identical state. This proves the crash-only design property holds.

### 10.4 Durability
- [ ] **TEST-340** [IO] Every `put` fsyncs the WAL before returning — mock IOEngine counting sync calls.
- [ ] **TEST-341** [IO] Flush fsyncs the SSTable **and** the directory before the WAL is deleted — assert call ordering, not just occurrence.
- [ ] **TEST-342** [IO] Compaction fsyncs the new SSTable before deleting inputs — assert ordering.
- [ ] **TEST-343** [IO] A failed WAL sync fails the `put` — the write must not appear in the memtable.

### 10.5 Workload / stress
- [ ] **TEST-350** [FUZZ] ⭐ **Model-based:** run 10k random operations (put/delete/get with a small key space to force overwrites and deletes) against both the engine and a `std::map` reference model. Assert every `get` matches the model. Interleave flushes and compactions at random points. **This is the single highest-value test in this document** — it finds bugs no hand-written case anticipates.
- [ ] **TEST-351** [FUZZ] Same, with a random crash-and-reopen injected every ~500 operations. Model must still match.
- [ ] **TEST-352** [PERF] 1M puts — memory stays bounded (flushes actually happen), file count stays bounded (compactions actually happen).
- [ ] **TEST-353** [IO] Keys and values at size extremes: 1 byte, 1KB, 1MB.
- [ ] **TEST-354** [IO] Very many distinct keys (1M) vs very few keys overwritten many times (10 keys × 100k writes).
- [ ] **TEST-355** [ASAN] Full workload under ASan and UBSan.

---

## Module 11 — Future modules (write these alongside the feature)

### Manifest (1L)
- [ ] **TEST-400** Crash after appending a manifest record but before deleting files → orphans reclaimed, no data loss.
- [ ] **TEST-401** Crash mid-manifest-append → torn record ignored, previous state intact.
- [ ] **TEST-402** Manifest and directory contents disagree → manifest wins, orphans logged.
- [ ] **TEST-403** Corrupt manifest → clean error, engine refuses to open rather than opening wrong.

### Scan path (1M)
- [ ] **TEST-410** Scan yields ascending order across memtable + all SSTables.
- [ ] **TEST-411** ⭐ Scan **never** surfaces a tombstone.
- [ ] **TEST-412** Half-open range semantics: `[start, end)` — start included, end excluded.
- [ ] **TEST-413** Unbounded start, unbounded end, both unbounded.
- [ ] **TEST-414** Empty range (start == end) → yields nothing.
- [ ] **TEST-415** Inverted range (start > end) → yields nothing or errors; define it.
- [ ] **TEST-416** Scan over a single partition returns exactly that partition's keys — proves the contiguity property the byte-comparable encoding buys.
- [ ] **TEST-417** [PROP] Scan results == filtered model, cross-checked against `std::map::lower_bound`/`upper_bound`.

---

## Suggested order

1. **Module 3** (key encoding) — untested, and everything rests on it. TEST-080 and TEST-087 first.
2. **TEST-350** (model-based fuzz) — highest bug-per-effort ratio in the whole document.
3. **Module 9** tombstone GC tests — alongside TICKET-130/131, since they define the predicate.
4. **TEST-300** (overwrite across flushes) — trivially cheap, currently uncovered.
5. **TEST-253** (merge error propagation) — a fixed bug with no regression test.
6. Everything else, module by module, as you touch each area.

---

## Notes on tooling

- **Property/fuzz tests:** RapidCheck integrates cleanly with GoogleTest. libFuzzer for the byte-level decoder fuzzing (TEST-107, TEST-160, TEST-219).
- **Crash simulation:** `fork()` + `_exit(1)` in the child gives a real uncommitted-state crash. Truncating files simulates torn writes without process death.
- **Mock IOEngine:** a counting/failing implementation of the `IOEngine` interface makes the durability-ordering tests (TEST-340–343) possible. Worth building early — several tests depend on it.
- **ASan/UBSan:** already enabled on Mac. Ensure the fuzz tests run under it, since that's where they earn their value.
