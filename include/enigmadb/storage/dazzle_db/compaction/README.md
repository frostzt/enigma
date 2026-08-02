# Compaction

This doc serves as the dump of thoughts and anything random I read around and about
compaction. Compaction in itself is a pretty complicated process and idea to get
right, therefore writing this doc. This will contain what's there and whats not
and what do we wanna do next.

## Papers and things found

- [Constructing and Analyzing the LSM Compaction Design Space](https://vldb.org/pvldb/vol14/p2216-sarkar.pdf)

## Current Design and Idea

Right now compaction and its process is FULLY owned by the storage engine (`storage_engine.h`)
the strategy and their logic should sit within this compaction directory.

### Strategies Available and built

- [x] Size Tiered Compaction
- [ ] Leveled Compaction

## Enigma's Size Tiered Compaction

The implemenation of Enigma's Size Tiered Compaction algorithm is pretty naive
and trivial. The idea is to pick up sstable files and compact them down into
one bigger sstable file.

## TODOs
