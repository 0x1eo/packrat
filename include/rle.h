/* Run-Length Encoding (optimized for zero runs) */

#ifndef RLE_H
#define RLE_H

#include <stddef.h>
#include <stdint.h>

void rle_encode(const uint8_t *input, size_t input_size, uint8_t *output, size_t *output_size);
int rle_decode(const uint8_t *input, size_t input_size, uint8_t *output, size_t max_output_size, size_t *output_size);

#endif /* RLE_H */
