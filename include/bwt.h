/* Burrows-Wheeler Transform */

#ifndef BWT_H
#define BWT_H

#include <stddef.h>
#include <stdint.h>

uint32_t bwt_encode(const uint8_t *input, uint8_t *output, size_t size);
void bwt_decode(const uint8_t *input, uint8_t *output, size_t size, uint32_t primary_index);

#endif /* BWT_H */
