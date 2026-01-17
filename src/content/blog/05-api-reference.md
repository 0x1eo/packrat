---
title: 'API Reference'
description: 'Function signatures and usage'
pubDate: 'Jan 17 2026'
---

Notes on the C API in case I need to embed this in something else later.

## Core functions

```c
#include "packrat.h"

// In-memory
int prt_compress(const uint8_t *input, size_t input_size,
                 uint8_t *output, size_t *output_size);
int prt_decompress(const uint8_t *input, size_t input_size,
                   uint8_t *output, size_t *output_size);

// Files
int prt_compress_file(const char *input_path, const char *output_path);
int prt_decompress_file(const char *input_path, const char *output_path);

// Get original size before decompressing (for buffer allocation)
size_t prt_get_original_size(const uint8_t *input, size_t input_size);
```

## Archives

```c
#include "archive.h"

prt_archive_t *prt_archive_create(const char *path);
prt_archive_t *prt_archive_open(const char *path);
void prt_archive_close(prt_archive_t *archive);

int prt_archive_add_dir(prt_archive_t *archive, 
                        const char *dir_path, const char *base_path);
int prt_archive_finalize(prt_archive_t *archive);
int prt_archive_extract_all(prt_archive_t *archive, const char *output_dir);
```

Solid archives use `archive_v3.h` - same pattern but `prt_solid_archive_*` functions.

## BWT directly

```c
#include "bwt.h"

uint32_t bwt_encode(const uint8_t *input, uint8_t *output, size_t size);
void bwt_decode(const uint8_t *input, uint8_t *output, size_t size, 
                uint32_t primary_index);
```

The encode returns the primary index needed for decode.

## Error codes

```c
#define PRT_OK              0
#define PRT_ERR_MEMORY     -1
#define PRT_ERR_FORMAT     -2
#define PRT_ERR_FILE       -3
#define PRT_ERR_CORRUPT    -4
```

## Example

```c
// Compress a buffer
uint8_t *input = read_file("data.txt", &input_size);
uint8_t *output = malloc(input_size + 1024);  // some headroom
size_t output_size = input_size + 1024;

if (prt_compress(input, input_size, output, &output_size) == PRT_OK) {
    write_file("data.prt", output, output_size);
}
```

All archive functions clean up on `_close()`. Memory for in-memory ops is caller's responsibility.
