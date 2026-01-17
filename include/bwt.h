/* Burrows-Wheeler Transform */

#ifndef BWT_H
#define BWT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define BWT_BLOCK_THRESHOLD_SAIS  (1 << 20)  /* 1MB threshold - use SAIS for better performance */
#define BWT_BLOCK_32MB            (32 << 20)

typedef enum {
    BWT_STRATEGY_AUTO,
    BWT_STRATEGY_QSORT,
    BWT_STRATEGY_SAIS,
} bwt_strategy_t;

typedef struct {
    bwt_strategy_t strategy;
    bool build_lcp;
    size_t max_memory;
    uint32_t num_threads;
} bwt_options_t;

#define BWT_OPTIONS_DEFAULT { BWT_STRATEGY_AUTO, false, 0, 0 }

uint32_t bwt_encode(const uint8_t *input, uint8_t *output, size_t size);
uint32_t bwt_encode_ex(const uint8_t *input, uint8_t *output, size_t size,
                        const bwt_options_t *options);

void bwt_decode(const uint8_t *input, uint8_t *output, size_t size, 
                uint32_t primary_index);

typedef struct {
    uint8_t *output;
    int32_t *lcp;
    uint32_t primary_index;
    double elapsed_ms;
} bwt_result_t;

int bwt_encode_full(const uint8_t *input, size_t size,
                    const bwt_options_t *options, bwt_result_t *result);

#endif /* BWT_H */
