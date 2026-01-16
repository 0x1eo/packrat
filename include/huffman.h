/* Canonical Huffman coding */

#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

#define HUFFMAN_MAX_CODE_LEN 32

typedef struct huffman_node {
    uint32_t freq;
    int symbol;
    struct huffman_node *left;
    struct huffman_node *right;
} huffman_node_t;

typedef struct {
    uint32_t code;
    uint8_t length;
} huffman_code_t;

void huffman_build_codes(const uint32_t *freq, huffman_code_t *codes);
uint8_t huffman_encode(const uint8_t *input, size_t input_size, const huffman_code_t *codes, uint8_t *output, size_t *output_size);
void huffman_decode(const uint8_t *input, size_t input_size, const uint8_t *code_lengths, uint8_t *output, size_t output_size, uint8_t padding_bits);

#endif /* HUFFMAN_H */
