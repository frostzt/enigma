On #2: encapsulating behind bump_*()/getters is unambiguously good — do that everywhere. But be clear-eyed: atomic counters do not make your write path thread-safe. Two threads can fetch_add cleanly (5 and 6) and still append to the WAL in the wrong order. Ordering the write path needs a mutex around {mint seq → WAL append → memtable insert}, and the atomics buy you nothing there. What they will buy you is lock-free reads of the current sequence when MVCC opens snapshots. Fine to have them now; just don't bank safety you don't have.

The tombstone rule, stated correctly

Your instinct broke on a special case, so here's the general rule. Forget "full vs partial" — that was always a proxy. The real predicate:

A tombstone may be dropped iff no SSTable outside the input set can hold an older version of that key.

Naively you'd check "is the oldest table in my inputs?" — but that's not sufficient. Counterexample: live tables SST-1 (x=5, seq 10), SST-2 (x=99, seq 20), SST-3 (tombstone, seq 40). Compact {1, 3}, skip 2. Oldest is included, so you'd drop the tombstone — and SST-2's x=99 resurrects.

The correct simple rule: the inputs must form a contiguous run down to the oldest. No live table outside the inputs may have an id lower than your highest input id. Since your ids are monotonic and your map is sorted, that's a cheap check.

Put it in exactly one place, as a free function both policies call:

cpp
// storage/compaction/tombstone_gc.h
bool can_drop_tombstones(const std::vector<SSTableMeta>& live,
                         const std::vector<SSTableId>& inputs);
// TODO(mvcc): also gate on oldest active snapshot sequence (Phase 4)

One definition, one place to harden later. Later refinements slot in here without touching any policy: per-key range overlap checks (RocksDB style), and the MVCC snapshot gate.

Policy interface

You now have two strategies (full, size-tiered), so the abstraction is earned. I'd move off std::variant + std::visit — you already have visit boilerplate in three places and it'll spread.

cpp
struct SSTableMeta {
    SSTableId id;
    uint64_t size_bytes, entry_count;
    uint64_t min_sequence, max_sequence;
    std::vector<uint8_t> min_key, max_key;   // for range checks later
};

struct CompactionTask {
    std::vector<SSTableId> inputs;
    bool can_drop_tombstones;   // DERIVED, never chosen by a caller
};

class CompactionPolicy {
public:
    virtual ~CompactionPolicy() = default;
    virtual std::optional<CompactionTask> pick(
        const std::vector<SSTableMeta>& live) const = 0;
    virtual std::string_view name() const = 0;
};

FullCompactionPolicy and SizeTieredPolicy implement it. Engine holds std::unique_ptr<CompactionPolicy>. Note can_drop_tombstones lives in the task, computed by the policy via the shared helper — so the executor just obeys it and can't be lied to.

For size-tiered selection: bucket tables by size (Cassandra uses tables within 0.5×–1.5× of the bucket average), pick a bucket with ≥ min_merge_width members, cap at max_merge_width. Prefer the oldest qualifying bucket — that biases toward contiguous-from-oldest picks, which means can_drop_tombstones is true more often. Nice property to design for deliberately.

Three phases — this is what makes it concurrency-ready

Structure the operation as three separate steps, because they have different locking needs:

Pick — reads the live set. Needs the lock. Microseconds.
Execute — merges inputs into a new SSTable. Needs no lock at all, because SSTables are immutable. Minutes.
Install — swap readers, record in manifest, delete old files. Needs the lock. Microseconds.

That's the whole reason background compaction works: the expensive phase touches only immutable data. If you keep these three physically separate now — even while calling them back-to-back synchronously — moving execute onto a thread later is nearly free. If you fuse them, you'll rewrite.

One trap to design for now: between pick and install, a flush may have added new SSTables, or a prior compaction may have removed your inputs. Install must re-validate that every input is still live and fail cleanly if not. Cheap to add now, painful to retrofit.

Manifest connection

Your #4 instinct is right and it lands exactly where this design needs it. The manifest becomes the authoritative answer to "which SSTables are live" — replacing directory scanning, and giving you SSTableMeta without opening every footer at startup. It also makes install atomic: append a record like {remove: [1..16], add: 17}, fsync, then delete files. Crash mid-delete leaves orphans the GC reclaims, never data loss. Do compaction policy first, then manifest — the manifest's record format wants to know what a CompactionTask looks like.

Start with can_drop_tombstones as a standalone function plus its unit tests — the contiguity rule, the counterexample above, the empty case. It's small, it's the thing that was eating you, and everything else depends on it being right. Want to sketch that predicate and bring it?
