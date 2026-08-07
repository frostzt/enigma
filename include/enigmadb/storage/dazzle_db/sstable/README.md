# SSTable

Here is how I am thinking my SSTable to be:

```plaintext

[SSTable File Layout]
+-----------------------------+
| Data Block 1               |
+-----------------------------+
| Data Block 2               |
+-----------------------------+
| ...                         |
+-----------------------------+
| Optional Filter Block(s)    | <- e.g. Bloom or Cuckoo
+-----------------------------+
| Index Block                |
+-----------------------------+
| Meta Block (optional)      | <- min/max keys, column info
+-----------------------------+
| Footer (fixed size)        |
+-----------------------------+

[Block Structure]
+-----------------------------+
| BlockHeader                |
|  - compressed_size         |
|  - uncompressed_size       |
|  - compression_type        |
|  - checksum (of compressed data)
+-----------------------------+
| Block Data (maybe compressed) |
+-----------------------------+

[Footer Structure (Fixed Size)]
+-----------------------------+
| index_block_offset (u64)   |
| filter_block_offset (u64)  |
| meta_block_offset (u64)    |
| compressor_id (u8)         |
| format_version (u8)        |
| footer_checksum (u32)      |
| magic[8] = "ENIGSSTB"      |
+-----------------------------+

```

## Current structure

<img width="2150" height="1073" alt="SSTable 1" src="https://github.com/user-attachments/assets/56e28965-3abb-4198-aacf-f185e068e301" />

```plaintext
[Data Block 0]
  Entry: key_len(4) | key | value_len(4) | value | is_tombstone(1) | sequence(8)
  Entry: ...
  Block size target: ~4KB (configurable)

[Data Block 1]
  Entry: ...

[Index Block]
  Entry: first_key_len(4) | first_key | block_offset(8) | block_size(4)
  Entry: ...

[Filter Block]
  num_hashes    (1)
  bit_array     (remaining bytes)

[Footer — fixed 64 bytes]
  index_block_offset  (8)
  index_block_size    (4)
  filter_block_offset (8)
  filter_block_size   (4)
  entry_count         (4)
  format_version      (2)
  highest_sequence    (8)
  size_bytes          (8)
  footer_checksum     (4)
  magic "ENIGSSTB"    (8)
  padding             (6)

```

## Choices and reasoning

1. On block size
I am going with 4KB as it seems like what's followed mostly,
on my current machine `getconf PAGESIZE` returns 16KB but that's
good anyways I get those for free. The reason not going big here
is that huge pages will never be the case this is a Database not
a Filesystem. 4KB will be easier to deal with and move around.

2. Entries and splitting
I am gonna go the under approach ie. if an entry pushes the size
over 4KB just pack it and create a new file and put it in there.
The reason for doing this is that this keeps entries complete
rather than creating impl like `varints` data blocks should be
complete in themselves.

3. Index Granularity
Honestly I am fine with loading the entire thing, 4KB isn't that
big of a deal tbh. Around 60-80 entries per block? Fine-ish. Later
on I think we can do some btree stuff to make it efficient.

## TODOs for later

- [ ] Compression -- lz4 or snappy
- [ ] Filter Blocks -- bloom and Cuckoo filters
- [ ] Meta Block -- metadata and stuff
- [ ] Btree -- Indexes can be improved?
- [ ] Offset Table -- Improve the index block parsing logic
