/* Move-to-Front Transform */

#ifndef MTF_H
#define MTF_H

#include <stddef.h>
#include <stdint.h>

void mtf_encode(const uint8_t *input, uint8_t *output, size_t size);
void mtf_decode(const uint8_t *input, uint8_t *output, size_t size);

#endif /* MTF_H */
