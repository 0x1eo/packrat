/* RUNA/RUNB encoding for zero runs */

#include "rle.h"
#include <string.h>

#define RUNA 0xFE
#define RUNB 0xFF
#define ESCAPE 0x00

void rle_encode(const uint8_t *input, size_t input_size,
                uint8_t *output, size_t *output_size) {
    size_t out_idx = 0;
    size_t i = 0;
    
    while (i < input_size) {
        if (input[i] == 0) {
            size_t run_length = 0;
            while (i < input_size && input[i] == 0) {
                run_length++;
                i++;
            }
            while (run_length > 0) {
                run_length--;
                output[out_idx++] = (run_length & 1) ? RUNB : RUNA;
                run_length >>= 1;
            }
        } else if (input[i] == RUNA) {
            output[out_idx++] = ESCAPE;
            output[out_idx++] = 0x00;
            i++;
        } else if (input[i] == RUNB) {
            output[out_idx++] = ESCAPE;
            output[out_idx++] = 0x01;
            i++;
        } else {
            output[out_idx++] = input[i++];
        }
    }
    *output_size = out_idx;
}

int rle_decode(const uint8_t *input, size_t input_size,
               uint8_t *output, size_t max_output_size,
               size_t *output_size) {
    size_t out_idx = 0;
    size_t i = 0;
    
    while (i < input_size) {
        if (input[i] == RUNA || input[i] == RUNB) {
            size_t run_length = 0;
            size_t power = 1;
            while (i < input_size && (input[i] == RUNA || input[i] == RUNB)) {
                run_length += (input[i] == RUNA) ? power : 2 * power;
                power <<= 1;
                i++;
            }
            if (out_idx + run_length > max_output_size) return -1;
            memset(output + out_idx, 0, run_length);
            out_idx += run_length;
        } else if (input[i] == ESCAPE) {
            i++;
            if (i >= input_size) return -1;
            if (out_idx >= max_output_size) return -1;
            output[out_idx++] = (input[i] == 0x00) ? RUNA : (input[i] == 0x01) ? RUNB : 0;
            if (input[i] != 0x00 && input[i] != 0x01) return -1;
            i++;
        } else {
            if (out_idx >= max_output_size) return -1;
            output[out_idx++] = input[i++];
        }
    }
    *output_size = out_idx;
    return 0;
}
