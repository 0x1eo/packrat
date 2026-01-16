# packrat

A compression tool that works well on text and source code.

Uses the BWT-MTF-RLE-Huffman pipeline (same approach as bzip2). Achieves around 10-20% ratios on typical source code.

## Build

```
make
```

## Usage

Compress a file:
```
./packrat -c myfile.txt myfile.prt
./packrat -c myfile.txt              # auto-names to myfile.txt.prt
```

Decompress:
```
./packrat -d myfile.prt output.txt
./packrat -d myfile.txt.prt          # auto-names to myfile.txt
```

Create an archive:
```
./packrat -a archive.prt src/
```

Create a solid archive (better compression, files grouped by type):
```
./packrat -a --solid archive.prt src/
```

Extract:
```
./packrat -x archive.prt dest/
```

List contents:
```
./packrat -l archive.prt
```

Show file info:
```
./packrat -i compressed.prt
```

## How it works

1. **BWT** - Burrows-Wheeler Transform groups similar characters together
2. **MTF** - Move-to-Front converts those clusters into runs of small numbers
3. **RLE** - Run-length encoding compresses the zero runs
4. **Huffman** - Entropy coding assigns short codes to common symbols

Solid archives concatenate files by type before compressing. All your `.c` files get compressed together, all your `.py` files together, etc. This exploits cross-file redundancy.

## File format

- `.prt` extension
- Magic bytes: `PRT\x02` (single file) or `PRT\x03` (solid archive)
- Stores original filename for auto-naming on decompress

## Tests

```
./tests/test_packrat.sh ./packrat
```

## License

Do whatever you want with it.
