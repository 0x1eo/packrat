---
title: 'The Compression Pipeline'
description: 'Notes on implementing BWT, MTF, RLE, and Huffman'
pubDate: 'Jan 17 2026'
---

These are my notes on implementing each stage of the compression pipeline.

## BWT - Burrows-Wheeler Transform

This is the clever part. BWT rearranges the input so that similar characters cluster together. The Wikipedia explanation made sense in theory but actually implementing it took a few tries.

The basic idea:
1. Generate all rotations of the string
2. Sort them
3. Take the last column

For `"banana"` you get `"nnbaaa"` - notice how the `a`s and `n`s grouped together. That's the whole point.

### The SA-IS rabbit hole

The naive BWT implementation sorted all rotations with qsort. Works fine, but O(n²) is brutal for anything over a few KB.

I ended up implementing SA-IS (Suffix Array by Induced Sorting). It's O(n) but the algorithm is... not simple. The key insight is that you can categorize suffixes as L-type or S-type based on their neighbors, then sort the "LMS" (leftmost S-type) suffixes first and induce the rest.

Took me a while to get the bucket indexing right. Off-by-one errors everywhere.

The implementation lives in `sais.c`. It's about 500 lines and I had to debug it with print statements for days.

## MTF - Move-to-Front

This one's simple. Keep a list of all 256 byte values. For each input byte, output its position in the list, then move that byte to the front.

```c
void mtf_encode(const uint8_t *input, uint8_t *output, size_t size) {
    uint8_t table[256];
    for (int i = 0; i < 256; i++) table[i] = i;
    
    for (size_t i = 0; i < size; i++) {
        uint8_t c = input[i];
        int pos = 0;
        while (table[pos] != c) pos++;
        output[i] = pos;
        memmove(table + 1, table, pos);
        table[0] = c;
    }
}
```

After BWT, similar characters are clustered. MTF turns those clusters into runs of zeros (position 0 = "same as last time"). That's what RLE will compress.

## RLE - Run-Length Encoding

First version was buggy. The issue was handling the escape byte - if your data contains `0xFE`, you need to escape it too.

Current approach:
- `0xFE` followed by value and count means "repeat this byte N times"
- `0xFE 0xFE` means a literal `0xFE`

Had some off-by-one issues with the count encoding too. 0 means 3 repeats (the minimum worth encoding), 255 means 258 repeats.

## Huffman coding

Standard canonical Huffman. Build a tree from frequencies, extract code lengths, generate codes from lengths.

The trick is limiting code length to 32 bits. Deep trees happen with skewed frequencies. Had to implement code length limiting which required reading a paper on optimal binary trees with length restrictions.

The decoder is table-driven. Precompute a lookup table indexed by the first N bits, then handle longer codes with a slower path.

## Putting it together

The pipeline in `packrat.c`:

```c
bwt_index = bwt_encode(input, bwt_out, size);
mtf_encode(bwt_out, mtf_out, size);
rle_encode(mtf_out, size, rle_out, &rle_size);
padding = huffman_encode(rle_out, rle_size, codes, huff_out, &huff_size);
```

Decompression is the reverse. Each step is invertible.

## Things I learned

- SA-IS is beautiful but hard to debug
- Off-by-one errors in compression are catastrophic - data comes out as garbage
- The BWT is reversible even though it looks like it shouldn't be (you only need the last column and the primary index)
- Huffman code length limiting is its own research topic
