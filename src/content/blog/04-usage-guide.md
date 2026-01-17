---
title: 'CLI Reference'
description: 'Command-line usage notes'
pubDate: 'Jan 17 2026'
---

Quick reference for the CLI. Mostly so I don't forget the flags.

## Compression

```bash
# Compress file
./packrat -c input.txt output.prt
./packrat -c input.txt  # auto-names to input.txt.prt

# Decompress
./packrat -d file.prt output.txt
./packrat -d input.txt.prt  # auto-names to input.txt
```

## Archives

```bash
# Create archive
./packrat -a archive.prt directory/

# Create solid archive (better compression)
./packrat -a --solid archive.prt directory/

# Extract
./packrat -x archive.prt destination/

# List contents
./packrat -l archive.prt
```

## Other

```bash
# Show info (sizes, ratio, original filename)
./packrat -i compressed.prt

# Test integrity without extracting
./packrat -t compressed.prt

# Help
./packrat -h
```

## Exit codes

- `0` - success
- `1` - generic error
- `2` - memory error
- `3` - format error
- `4` - file error
- `5` - corrupt data

## Typical ratios

On text/source code, expect 10-25% of original size. Binary files vary. Already-compressed files (jpg, png, zip) won't compress further - packrat detects high entropy and stores them uncompressed to avoid expansion.
