/* packrat compression pipeline: BWT -> MTF -> RLE -> Huffman */

#include "packrat.h"
#include "bwt.h"
#include "mtf.h"
#include "rle.h"
#include "huffman.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <libgen.h>

static const uint8_t PRT_MAGIC_V2[4] = {'C', 'M', 'P', 0x02};

static void write_u32_le(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

static uint32_t read_u32_le(const uint8_t *buf) {
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24);
}

static const char* get_basename(const char *path) {
    const char *base = strrchr(path, '/');
    return base ? base + 1 : path;
}

/* Internal compress with filename */
static int prt_compress_internal(const uint8_t *input, size_t input_size,
                                  const char *filename,
                                  uint8_t *output, size_t *output_size) {
    if (input_size == 0) {
        memcpy(output, PRT_MAGIC_V2, PRT_MAGIC_SIZE);
        memset(output + PRT_MAGIC_SIZE, 0, PRT_HEADER_SIZE - PRT_MAGIC_SIZE);
        *output_size = PRT_HEADER_SIZE;
        return PRT_OK;
    }
    
    uint8_t *bwt_out = malloc(input_size);
    uint8_t *mtf_out = malloc(input_size);
    uint8_t *rle_out = malloc(input_size * 2);
    uint8_t *huff_out = malloc(input_size * 2);
    
    if (!bwt_out || !mtf_out || !rle_out || !huff_out) {
        free(bwt_out);
        free(mtf_out);
        free(rle_out);
        free(huff_out);
        return PRT_ERR_MEMORY;
    }
    
    uint32_t bwt_index = bwt_encode(input, bwt_out, input_size);
    mtf_encode(bwt_out, mtf_out, input_size);
    
    size_t rle_size;
    rle_encode(mtf_out, input_size, rle_out, &rle_size);
    
    uint32_t freq[256] = {0};
    for (size_t i = 0; i < rle_size; i++) freq[rle_out[i]]++;
    
    huffman_code_t codes[256];
    huffman_build_codes(freq, codes);
    
    size_t huff_size;
    uint8_t padding = huffman_encode(rle_out, rle_size, codes, huff_out, &huff_size);
    
    size_t filename_len = filename ? strlen(filename) : 0;
    if (filename_len > PRT_MAX_FILENAME - 1) filename_len = PRT_MAX_FILENAME - 1;
    
    uint8_t *p = output;
    memcpy(p, PRT_MAGIC_V2, 4); p += 4;
    write_u32_le(p, (uint32_t)input_size); p += 4;
    write_u32_le(p, bwt_index); p += 4;
    write_u32_le(p, (uint32_t)rle_size); p += 4;
    *p++ = padding;
    *p++ = (uint8_t)filename_len;
    *p++ = 0;
    *p++ = 0;
    
    memset(p, 0, PRT_MAX_FILENAME);
    if (filename && filename_len > 0) memcpy(p, filename, filename_len);
    p += PRT_MAX_FILENAME;
    
    for (int i = 0; i < 256; i++) *p++ = codes[i].length;
    
    memcpy(p, huff_out, huff_size);
    *output_size = PRT_HEADER_SIZE + huff_size;
    
    free(bwt_out);
    free(mtf_out);
    free(rle_out);
    free(huff_out);
    
    return PRT_OK;
}

int prt_compress(const uint8_t *input, size_t input_size,
                 uint8_t *output, size_t *output_size) {
    return prt_compress_internal(input, input_size, NULL, output, output_size);
}

int prt_decompress(const uint8_t *input, size_t input_size,
                   uint8_t *output, size_t *output_size) {
    if (input_size < PRT_HEADER_SIZE) return PRT_ERR_FORMAT;
    
    if (memcmp(input, PRT_MAGIC, PRT_MAGIC_SIZE) != 0 &&
        memcmp(input, PRT_MAGIC_V2, PRT_MAGIC_SIZE) != 0) {
        return PRT_ERR_FORMAT;
    }
    
    const uint8_t *p = input + 4;
    uint32_t original_size = read_u32_le(p); p += 4;
    uint32_t bwt_index = read_u32_le(p); p += 4;
    uint32_t rle_size = read_u32_le(p); p += 4;
    uint8_t padding_bits = *p++;
    p += 1 + 2 + PRT_MAX_FILENAME;
    const uint8_t *code_lengths = p; p += 256;
    
    if (original_size == 0) {
        *output_size = 0;
        return PRT_OK;
    }
    
    if (*output_size < original_size) return PRT_ERR_MEMORY;
    
    size_t huff_size = input_size - PRT_HEADER_SIZE;
    
    uint8_t *rle_out = malloc(rle_size);
    uint8_t *mtf_out = malloc(original_size);
    uint8_t *bwt_out = malloc(original_size);
    
    if (!rle_out || !mtf_out || !bwt_out) {
        free(rle_out); free(mtf_out); free(bwt_out);
        return PRT_ERR_MEMORY;
    }
    
    huffman_decode(p, huff_size, code_lengths, rle_out, rle_size, padding_bits);
    
    size_t mtf_size;
    if (rle_decode(rle_out, rle_size, mtf_out, original_size, &mtf_size) != 0) {
        free(rle_out); free(mtf_out); free(bwt_out);
        return PRT_ERR_CORRUPT;
    }
    
    mtf_decode(mtf_out, bwt_out, original_size);
    bwt_decode(bwt_out, output, original_size, bwt_index);
    
    *output_size = original_size;
    
    free(rle_out); free(mtf_out); free(bwt_out);
    return PRT_OK;
}

int prt_compress_file(const char *input_path, const char *output_path) {
    FILE *fin = fopen(input_path, "rb");
    if (!fin) return PRT_ERR_FILE;
    
    fseek(fin, 0, SEEK_END);
    size_t input_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *input = malloc(input_size);
    if (!input) {
        fclose(fin);
        return PRT_ERR_MEMORY;
    }
    
    if (fread(input, 1, input_size, fin) != input_size) {
        free(input);
        fclose(fin);
        return PRT_ERR_FILE;
    }
    fclose(fin);
    
    /* Get basename for storage */
    const char *base = get_basename(input_path);
    
    /* Compress */
    size_t output_max = input_size + PRT_HEADER_SIZE + 1024;
    uint8_t *output = malloc(output_max);
    if (!output) {
        free(input);
        return PRT_ERR_MEMORY;
    }
    
    size_t output_size;
    int result = prt_compress_internal(input, input_size, base, output, &output_size);
    free(input);
    
    if (result != PRT_OK) {
        free(output);
        return result;
    }
    
    /* Write output */
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        free(output);
        return PRT_ERR_FILE;
    }
    
    if (fwrite(output, 1, output_size, fout) != output_size) {
        free(output);
        fclose(fout);
        return PRT_ERR_FILE;
    }
    
    free(output);
    fclose(fout);
    return PRT_OK;
}

int prt_compress_file_auto(const char *input_path, char *output_path) {
    snprintf(output_path, 4096, "%s.prt", input_path);
    return prt_compress_file(input_path, output_path);
}

int prt_decompress_file(const char *input_path, const char *output_path) {
    FILE *fin = fopen(input_path, "rb");
    if (!fin) return PRT_ERR_FILE;
    
    fseek(fin, 0, SEEK_END);
    size_t input_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *input = malloc(input_size);
    if (!input) {
        fclose(fin);
        return PRT_ERR_MEMORY;
    }
    
    if (fread(input, 1, input_size, fin) != input_size) {
        free(input);
        fclose(fin);
        return PRT_ERR_FILE;
    }
    fclose(fin);
    
    size_t output_size = prt_get_original_size(input, input_size);
    if (output_size == 0 && input_size > PRT_HEADER_SIZE) {
        free(input);
        return PRT_ERR_FORMAT;
    }
    
    uint8_t *output = malloc(output_size + 1);
    if (!output && output_size > 0) {
        free(input);
        return PRT_ERR_MEMORY;
    }
    
    int result = prt_decompress(input, input_size, output, &output_size);
    free(input);
    
    if (result != PRT_OK) {
        free(output);
        return result;
    }
    
    FILE *fout = fopen(output_path, "wb");
    if (!fout) {
        free(output);
        return PRT_ERR_FILE;
    }
    
    if (output_size > 0 && fwrite(output, 1, output_size, fout) != output_size) {
        free(output);
        fclose(fout);
        return PRT_ERR_FILE;
    }
    
    free(output);
    fclose(fout);
    return PRT_OK;
}

int prt_decompress_file_auto(const char *input_path, char *output_path) {
    /* Read header to get original filename */
    FILE *fin = fopen(input_path, "rb");
    if (!fin) return PRT_ERR_FILE;
    
    uint8_t header[PRT_HEADER_SIZE];
    if (fread(header, 1, PRT_HEADER_SIZE, fin) != PRT_HEADER_SIZE) {
        fclose(fin);
        return PRT_ERR_FORMAT;
    }
    fclose(fin);
    
    /* Get stored filename */
    char filename[PRT_MAX_FILENAME];
    size_t len = prt_get_original_filename(header, PRT_HEADER_SIZE, filename);
    
    if (len > 0) {
        /* Get directory of input file */
        char input_copy[4096];
        strncpy(input_copy, input_path, sizeof(input_copy) - 1);
        input_copy[sizeof(input_copy) - 1] = '\0';
        
        char *dir = input_copy;
        char *last_slash = strrchr(dir, '/');
        if (last_slash) {
            *last_slash = '\0';
            snprintf(output_path, 4096, "%s/%s", dir, filename);
        } else {
            strncpy(output_path, filename, 4096);
        }
    } else {
        /* No filename stored, strip .prt extension */
        strncpy(output_path, input_path, 4096);
        size_t path_len = strlen(output_path);
        if (path_len > 4 && strcmp(output_path + path_len - 4, ".prt") == 0) {
            output_path[path_len - 4] = '\0';
        } else {
            strncat(output_path, ".out", 4096 - path_len - 1);
        }
    }
    
    return prt_decompress_file(input_path, output_path);
}

size_t prt_get_original_size(const uint8_t *input, size_t input_size) {
    if (input_size < PRT_HEADER_SIZE) return 0;
    if (memcmp(input, PRT_MAGIC, PRT_MAGIC_SIZE) != 0 &&
        memcmp(input, PRT_MAGIC_V2, PRT_MAGIC_SIZE) != 0) return 0;
    
    return read_u32_le(input + 4);
}

size_t prt_get_original_filename(const uint8_t *input, size_t input_size, char *filename) {
    if (input_size < PRT_HEADER_SIZE) return 0;
    
    /* Only v2 format has filename */
    if (memcmp(input, PRT_MAGIC_V2, PRT_MAGIC_SIZE) != 0) {
        filename[0] = '\0';
        return 0;
    }
    
    uint8_t filename_len = input[17];  /* Offset: 4+4+4+4+1 = 17 */
    if (filename_len == 0 || filename_len >= PRT_MAX_FILENAME) {
        filename[0] = '\0';
        return 0;
    }
    
    const char *stored = (const char *)(input + 20);  /* Offset: 4+4+4+4+1+1+2 = 20 */
    memcpy(filename, stored, filename_len);
    filename[filename_len] = '\0';
    
    return filename_len;
}
