/* SA-IS (Suffix Array Induced Sorting) - Linear Time Suffix Array Construction */

#ifndef SAIS_H
#define SAIS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAIS_BLOCK_1MB      (1 << 20)
#define SAIS_BLOCK_32MB     (32 << 20)
#define SAIS_SAMPLE_SIZE    (64 << 10)
#define SAIS_ALPHABET_SIZE  256

#define SAIS_MAX_RSS_BYTES  (200 << 20)
#define SAIS_ENTROPY_HIGH   7.5
#define SAIS_ENTROPY_LOW    4.0

#define SAIS_OK              0
#define SAIS_ERR_MEMORY     -1
#define SAIS_ERR_INPUT      -2
#define SAIS_ERR_OVERFLOW   -3
#define SAIS_ERR_ENTROPY    -4

typedef enum {
    BLOCK_MODE_SOLID_32MB,
    BLOCK_MODE_SPLIT_1MB,
    BLOCK_MODE_STORED,
} sais_block_mode_t;

typedef struct {
    double entropy;
    double redundancy_score;
    uint32_t unique_bytes;
    uint32_t run_count;
    sais_block_mode_t mode;
} sais_block_score_t;

int sais_compute_block_score(const uint8_t *data, size_t size, 
                              size_t sample_pos, sais_block_score_t *score);

int sais_build_sa(const uint8_t *T, int32_t *SA, size_t n);

int sais_build_sa_bounded(const uint8_t *T, int32_t *SA, size_t n, 
                          size_t max_memory);

int sais_build_lcp(const uint8_t *T, const int32_t *SA, int32_t *LCP, size_t n);
int sais_build_lcp_plcp(const uint8_t *T, const int32_t *SA, 
                        int32_t *LCP, size_t n);

int sais_build_bwt(const uint8_t *T, const int32_t *SA, uint8_t *BWT,
                   size_t n, uint32_t *primary_index);


typedef struct {
    uint32_t num_threads;
    uint32_t affinity_mask;
    size_t per_thread_memory;
} sais_thread_config_t;


typedef struct {
    const uint8_t *data;
    size_t size;
    uint32_t group_id;
    uint32_t content_type;
    sais_block_score_t score;
} sais_solid_group_t;

typedef struct {
    int32_t *SA;
    int32_t *LCP;
    uint8_t *BWT;
    uint32_t primary_index;
    double elapsed_ms;
    size_t peak_memory;
} sais_result_t;

int sais_thread_pool_init(const sais_thread_config_t *config);

void sais_thread_pool_shutdown(void);

int sais_process_groups_parallel(const sais_solid_group_t *groups,
                                  size_t num_groups,
                                  sais_result_t *results,
                                  bool build_lcp);

size_t sais_estimate_memory(size_t n, bool with_lcp);

bool sais_validate_sa(const uint8_t *T, const int32_t *SA, size_t n);
bool sais_validate_lcp(const uint8_t *T, const int32_t *SA, 
                       const int32_t *LCP, size_t n);

#ifdef __cplusplus
}
#endif

#endif /* SAIS_H */
