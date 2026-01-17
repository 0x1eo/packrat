/* packrat - Text compression library (BWT + MTF + RLE + Huffman) */

#ifndef PACKRAT_H
#define PACKRAT_H

#include <stddef.h>
#include <stdint.h>

#define PRT_MAGIC "PRT\x01"
#define PRT_MAGIC_SIZE 4

#define PRT_OK              0
#define PRT_ERR_MEMORY     -1
#define PRT_ERR_FORMAT     -2
#define PRT_ERR_FILE       -3
#define PRT_ERR_CORRUPT    -4
#define PRT_ERR_PATH_TOO_LONG  -11

#define PRT_MAX_FILENAME 128

typedef struct {
    uint8_t  magic[4];
    uint32_t original_size;
    uint32_t bwt_index;
    uint32_t rle_size;
    uint8_t  padding_bits;
    uint8_t  filename_len;
    uint8_t  reserved[2];
    char     filename[PRT_MAX_FILENAME];
    uint8_t  code_lengths[256];
} prt_header_t;

#define PRT_HEADER_SIZE (4 + 4 + 4 + 4 + 1 + 1 + 2 + PRT_MAX_FILENAME + 256)

/* In-memory compression */
int prt_compress(const uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size);
int prt_decompress(const uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size);

/* File compression */
int prt_compress_file(const char *input_path, const char *output_path);
int prt_compress_file_auto(const char *input_path, char *output_path);
int prt_decompress_file(const char *input_path, const char *output_path);
int prt_decompress_file_auto(const char *input_path, char *output_path);

/* Header inspection */
size_t prt_get_original_size(const uint8_t *input, size_t input_size);
size_t prt_get_original_filename(const uint8_t *input, size_t input_size, char *filename);

#endif /* PACKRAT_H */
