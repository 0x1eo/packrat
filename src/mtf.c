#include "mtf.h"
#include <string.h>

void mtf_encode(const uint8_t *input, uint8_t *output, size_t size) {
    uint8_t list[256];
    for (int i = 0; i < 256; i++) list[i] = (uint8_t)i;
    
    for (size_t i = 0; i < size; i++) {
        uint8_t c = input[i];
        uint8_t pos = 0;
        while (list[pos] != c) pos++;
        output[i] = pos;
        if (pos > 0) {
            memmove(&list[1], &list[0], pos);
            list[0] = c;
        }
    }
}

void mtf_decode(const uint8_t *input, uint8_t *output, size_t size) {
    uint8_t list[256];
    for (int i = 0; i < 256; i++) list[i] = (uint8_t)i;
    
    for (size_t i = 0; i < size; i++) {
        uint8_t pos = input[i];
        uint8_t c = list[pos];
        output[i] = c;
        if (pos > 0) {
            memmove(&list[1], &list[0], pos);
            list[0] = c;
        }
    }
}
