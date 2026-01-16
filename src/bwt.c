/* BWT using qsort_r (thread-safe (probably, maybe )) */

#define _GNU_SOURCE
#include "bwt.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    const uint8_t *data;
    size_t size;
} bwt_context_t;

static int compare_rotations(const void *a, const void *b, void *ctx) {
    bwt_context_t *c = (bwt_context_t *)ctx;
    size_t i = *(const size_t *)a;
    size_t j = *(const size_t *)b;
    const uint8_t *data = c->data;
    size_t size = c->size;
    
    /* Limit depth to avoid slow comparisons on repetitive data */
    size_t max_cmp = size < 4096 ? size : 4096;
    
    for (size_t k = 0; k < max_cmp; k++) {
        size_t pi = (i + k) % size;
        size_t pj = (j + k) % size;
        if (data[pi] != data[pj]) {
            return (int)data[pi] - (int)data[pj];
        }
    }
    return (i < j) ? -1 : (i > j) ? 1 : 0;
}

uint32_t bwt_encode(const uint8_t *input, uint8_t *output, size_t size) {
    if (size == 0) return 0;
    if (size == 1) {
        output[0] = input[0];
        return 0;
    }
    
    size_t *indices = malloc(size * sizeof(size_t));
    if (!indices) return 0;
    
    for (size_t i = 0; i < size; i++) {
        indices[i] = i;
    }
    
    bwt_context_t ctx = { input, size };
    qsort_r(indices, size, sizeof(size_t), compare_rotations, &ctx);
    
    uint32_t primary_index = 0;
    for (size_t i = 0; i < size; i++) {
        if (indices[i] == 0) {
            primary_index = (uint32_t)i;
        }
        output[i] = input[(indices[i] + size - 1) % size];
    }
    
    free(indices);
    return primary_index;
}

void bwt_decode(const uint8_t *input, uint8_t *output, size_t size, uint32_t primary_index) {
    if (size == 0) return;
    if (size == 1) {
        output[0] = input[0];
        return;
    }
    
    size_t count[256] = {0};
    for (size_t i = 0; i < size; i++) {
        count[input[i]]++;
    }
    
    size_t cumulative[256];
    size_t sum = 0;
    for (int c = 0; c < 256; c++) {
        cumulative[c] = sum;
        sum += count[c];
    }
    
    size_t *T = malloc(size * sizeof(size_t));
    if (!T) return;
    
    memset(count, 0, sizeof(count));
    for (size_t i = 0; i < size; i++) {
        uint8_t c = input[i];
        T[i] = cumulative[c] + count[c];
        count[c]++;
    }
    
    size_t idx = primary_index;
    for (size_t i = size; i > 0; i--) {
        output[i - 1] = input[idx];
        idx = T[idx];
    }
    
    free(T);
}

