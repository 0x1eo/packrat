/* BWT Implementation with SA-IS Integration */

#define _GNU_SOURCE
#include "bwt.h"
#include "sais.h"
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

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
    
    for (size_t k = 0; k < size; k++) {
        size_t pi = (i + k) % size;
        size_t pj = (j + k) % size;
        if (data[pi] != data[pj]) {
            return (int)data[pi] - (int)data[pj];
        }
    }
    return (i < j) ? -1 : (i > j) ? 1 : 0;
}

static uint32_t bwt_encode_qsort(const uint8_t *input, uint8_t *output, size_t size) {
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

static uint32_t bwt_encode_sais(const uint8_t *input, uint8_t *output, 
                                 size_t size, int32_t **out_lcp) {
    int32_t *SA = malloc(size * sizeof(int32_t));
    if (!SA) return 0;
    
    int err = sais_build_sa(input, SA, size);
    if (err != SAIS_OK) {
        free(SA);
        return 0;
    }
    
    uint32_t primary_index = 0;
    sais_build_bwt(input, SA, output, size, &primary_index);
    
    if (out_lcp) {
        *out_lcp = malloc(size * sizeof(int32_t));
        if (*out_lcp) {
            sais_build_lcp(input, SA, *out_lcp, size);
        }
    }
    
    free(SA);
    return primary_index;
}

static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000.0 + tv.tv_usec / 1000.0;
}

uint32_t bwt_encode(const uint8_t *input, uint8_t *output, size_t size) {
    bwt_options_t opts = BWT_OPTIONS_DEFAULT;
    return bwt_encode_ex(input, output, size, &opts);
}

uint32_t bwt_encode_ex(const uint8_t *input, uint8_t *output, size_t size,
                        const bwt_options_t *options) {
    if (size == 0) return 0;
    if (size == 1) {
        output[0] = input[0];
        return 0;
    }
    
    bwt_options_t opts = options ? *options : (bwt_options_t)BWT_OPTIONS_DEFAULT;
    
    bwt_strategy_t strategy = opts.strategy;
    if (strategy == BWT_STRATEGY_AUTO) {
        if (size >= BWT_BLOCK_THRESHOLD_SAIS) {
            strategy = BWT_STRATEGY_SAIS;
        } else {
            strategy = BWT_STRATEGY_QSORT;
        }
    }
    
    if (strategy == BWT_STRATEGY_SAIS && opts.max_memory > 0) {
        size_t required = sais_estimate_memory(size, opts.build_lcp);
        if (required > opts.max_memory) {
            strategy = BWT_STRATEGY_QSORT;
        }
    }
    
    if (strategy == BWT_STRATEGY_SAIS) {
        return bwt_encode_sais(input, output, size, NULL);
    } else {
        return bwt_encode_qsort(input, output, size);
    }
}

int bwt_encode_full(const uint8_t *input, size_t size,
                    const bwt_options_t *options, bwt_result_t *result) {
    if (!input || !result || !result->output) return -1;
    
    double start = get_time_ms();
    
    bwt_options_t opts = options ? *options : (bwt_options_t)BWT_OPTIONS_DEFAULT;
    
    result->lcp = NULL;
    result->primary_index = 0;
    
    if (size == 0) {
        result->elapsed_ms = 0;
        return 0;
    }
    
    if (size == 1) {
        result->output[0] = input[0];
        result->elapsed_ms = get_time_ms() - start;
        return 0;
    }
    
    if (size >= BWT_BLOCK_THRESHOLD_SAIS || opts.build_lcp) {
        result->primary_index = bwt_encode_sais(input, result->output, size,
                                                 opts.build_lcp ? &result->lcp : NULL);
    } else {
        result->primary_index = bwt_encode_qsort(input, result->output, size);
    }
    
    result->elapsed_ms = get_time_ms() - start;
    return 0;
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

