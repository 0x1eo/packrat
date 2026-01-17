---
title: 'Building a Compression Tool'
description: 'Why I started this project and what I set out to learn'
pubDate: 'Jan 17 2026'
---

I wanted to understand how compression actually works. Not just "use zlib" but the actual algorithms underneath.

The goal was to build something from scratch that could compress text files reasonably well. I picked the BWT pipeline because it's what bzip2 uses, and I'd read that it works especially well on text.

## The plan

After some research, the approach is:

1. **BWT** - Burrows-Wheeler Transform, rearranges bytes
2. **MTF** - Move-to-Front, converts to small numbers
3. **RLE** - Run-Length Encoding, compresses repeated bytes
4. **Huffman** - Variable-length codes based on frequency

Each step feeds into the next. I'll document what I learn as I go.

## First attempts

Getting BWT working was the first milestone. The naive O(n²) sort was fine for testing but way too slow for real files. Eventually implemented SA-IS which is O(n) - that was a whole learning experience on its own.

The other pieces (MTF, RLE, Huffman) were more straightforward once I understood what they needed to do.

## What's in these notes

- How the compression pipeline works (what I learned implementing it)
- Solid archives (the optimization I added later for directories)
- CLI usage (reference for myself)
- API docs (in case I forget the function signatures)

This isn't a tutorial. It's documentation of what I built and notes on what I figured out along the way.
