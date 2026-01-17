---
title: 'Solid Archives'
description: 'Adding cross-file compression for better ratios'
pubDate: 'Jan 17 2026'
---

After getting single-file compression working, I noticed something: compressing a directory of source files one-by-one gave worse ratios than concatenating them first.

Makes sense. If every `.c` file has `#include <stdio.h>`, the BWT in each file only sees that pattern a few times. Concatenate them all and suddenly it sees it hundreds of times - much stronger statistical patterns.

## The idea

Instead of:
```
file1.c → compress → block1
file2.c → compress → block2
```

Do:
```
file1.c + file2.c → compress → single block
```

This is called "solid" compression. 7-zip does this too.

## File grouping

Can't just concatenate everything - mixing C code with JPEG images would hurt compression. Different file types have different statistical properties.

So I group files by extension before concatenating:

- `.c`, `.h` → C group
- `.py` → Python group  
- `.js`, `.ts` → JavaScript group
- etc.

The grouping logic is in `prt_get_file_group()`. It's just a big switch statement on the extension.

## Implementation

The archive format has two tables:
1. **Block table** - where each compressed block starts, its size, which group
2. **File table** - path, size, which block it's in, offset within block

When creating an archive:
1. Scan directory, bucket files by group
2. For each group, concatenate file contents
3. Compress the concatenated blob
4. Write block to archive
5. Record file offsets within the block

When extracting:
1. Read tables
2. For each file, decompress its block (if not already), seek to offset, copy bytes

## The trade-off

Solid archives are slower for random access. To extract one file, you might need to decompress the whole block it's in.

For backups and distribution this is fine - you usually extract everything anyway. For archives you access frequently, per-file compression is better.

## Results

On a typical source tree:

| Mode | Ratio |
|------|-------|
| Per-file | ~25% |
| Solid | ~15% |

That's a big improvement. Cross-file redundancy is real.

## The `--solid` flag

```bash
./packrat -a --solid archive.prt ./src
```

Without `--solid`, it falls back to per-file compression (archive v1 format). With it, uses solid blocks (archive v3 format).

The format is auto-detected on extraction - both work with `./packrat -x`.
