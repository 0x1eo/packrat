/* SA-IS (Suffix Array Induced Sorting) Implementation */

#define _GNU_SOURCE
#include "sais.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#include <errno.h>

#define STYPE 1
#define LTYPE 0

#define EMPTY_SLOT (-1)

#define TYPE_BIT(t, i)     (((t)[(i) >> 3] >> ((i) & 7)) & 1)
#define SET_STYPE(t, i)    ((t)[(i) >> 3] |= (1 << ((i) & 7)))
#define SET_LTYPE(t, i)    ((t)[(i) >> 3] &= ~(1 << ((i) & 7)))

#define IS_LMS(t, i)       ((i) > 0 && TYPE_BIT(t, i) == STYPE && TYPE_BIT(t, (i)-1) == LTYPE)

static double compute_entropy(const uint8_t *data, size_t size) {
    if (size == 0) return 0.0;
    
    uint64_t freq[256] = {0};
    for (size_t i = 0; i < size; i++) {
        freq[data[i]]++;
    }
    
    double entropy = 0.0;
    double n = (double)size;
    
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            double p = (double)freq[i] / n;
            entropy -= p * log2(p);
        }
    }
    
    return entropy;
}

static void analyze_patterns(const uint8_t *data, size_t size,
                             uint32_t *unique_bytes, uint32_t *run_count) {
    uint8_t seen[256] = {0};
    uint32_t unique = 0;
    uint32_t runs = 0;
    
    if (size == 0) {
        *unique_bytes = 0;
        *run_count = 0;
        return;
    }
    
    uint8_t prev = data[0];
    size_t run_len = 1;
    
    if (!seen[prev]) {
        seen[prev] = 1;
        unique++;
    }
    
    for (size_t i = 1; i < size; i++) {
        uint8_t c = data[i];
        if (!seen[c]) {
            seen[c] = 1;
            unique++;
        }
        
        if (c == prev) {
            run_len++;
        } else {
            if (run_len >= 4) {
                runs++;
            }
            prev = c;
            run_len = 1;
        }
    }
    
    if (run_len >= 4) {
        runs++;
    }
    
    *unique_bytes = unique;
    *run_count = runs;
}

static double compute_redundancy_score(const uint8_t *data, size_t size) {
    if (size < 4) return 0.0;
    
    /* Sample-based 4-gram analysis for speed */
    size_t sample_step = (size > 16384) ? (size / 4096) : 1;
    size_t samples = 0;
    size_t repeated = 0;
    
    /* Simple hash table for 4-gram detection */
    #define HASH_SIZE 65536
    static uint8_t hash_table[HASH_SIZE];
    memset(hash_table, 0, sizeof(hash_table));
    
    for (size_t i = 0; i + 4 <= size; i += sample_step) {
        uint32_t gram = (data[i] << 24) | (data[i+1] << 16) | 
                        (data[i+2] << 8) | data[i+3];
        uint32_t hash = (gram * 2654435761U) % HASH_SIZE;
        
        if (hash_table[hash]) {
            repeated++;
        } else {
            hash_table[hash] = 1;
        }
        samples++;
    }
    #undef HASH_SIZE
    
    return (samples > 0) ? ((double)repeated / (double)samples) : 0.0;
}

int sais_compute_block_score(const uint8_t *data, size_t size,
                              size_t sample_pos, sais_block_score_t *score) {
    if (!data || !score || size == 0) {
        return SAIS_ERR_INPUT;
    }
    
    /* Determine sample region */
    size_t sample_size = SAIS_SAMPLE_SIZE;
    if (sample_pos + sample_size > size) {
        sample_size = size - sample_pos;
    }
    if (sample_size > size) {
        sample_size = size;
        sample_pos = 0;
    }
    
    const uint8_t *sample = data + sample_pos;
    
    score->entropy = compute_entropy(sample, sample_size);
    
    analyze_patterns(sample, sample_size, 
                     &score->unique_bytes, &score->run_count);
    
    score->redundancy_score = compute_redundancy_score(sample, sample_size);
    
    if (score->entropy > SAIS_ENTROPY_HIGH) {
        if (score->entropy > 7.9) {
            score->mode = BLOCK_MODE_STORED;
        } else {
            score->mode = BLOCK_MODE_SPLIT_1MB;
        }
    } else if (score->entropy < SAIS_ENTROPY_LOW && score->redundancy_score > 0.3) {
        score->mode = BLOCK_MODE_SOLID_32MB;
    } else if (size < SAIS_BLOCK_1MB) {
        score->mode = BLOCK_MODE_SPLIT_1MB;
    } else {
        score->mode = (size >= SAIS_BLOCK_32MB / 2) ? 
                      BLOCK_MODE_SOLID_32MB : BLOCK_MODE_SPLIT_1MB;
    }
    
    return SAIS_OK;
}

static void get_buckets(const uint8_t *T, size_t n, int K,
                        int32_t *bucket, int end) {
    memset(bucket, 0, K * sizeof(int32_t));
    
    for (size_t i = 0; i < n; i++) {
        bucket[T[i]]++;
    }
    
    int32_t sum = 0;
    if (end) {
        for (int i = 0; i < K; i++) {
            sum += bucket[i];
            bucket[i] = sum;
        }
    } else {
        for (int i = 0; i < K; i++) {
            int32_t tmp = bucket[i];
            bucket[i] = sum;
            sum += tmp;
        }
    }
}

static void get_buckets_int(const int32_t *T, size_t n, int K,
                            int32_t *bucket, int end) {
    memset(bucket, 0, K * sizeof(int32_t));
    
    for (size_t i = 0; i < n; i++) {
        bucket[T[i]]++;
    }
    
    int32_t sum = 0;
    if (end) {
        for (int i = 0; i < K; i++) {
            sum += bucket[i];
            bucket[i] = sum;
        }
    } else {
        for (int i = 0; i < K; i++) {
            int32_t tmp = bucket[i];
            bucket[i] = sum;
            sum += tmp;
        }
    }
}

/**
 * InducedSort - The heart of SA-IS algorithm
 * 
 * This function performs the "induced sorting" phase which is what makes
 * SA-IS linear time. The key insight is:
 * 
 * 1. If we know the sorted positions of all LMS suffixes, we can INDUCE
 *    the positions of all L-type suffixes by scanning left-to-right.
 * 
 * 2. Similarly, we can induce S-type suffix positions by scanning right-to-left.
 * 
 * The induction works because:
 * - If SA[i] = j and j > 0 and T[j-1] >= T[j], then T[j-1..] is L-type
 *   and belongs in the L-type region of bucket T[j-1]
 * - If SA[i] = j and j > 0 and T[j-1] <= T[j], then T[j-1..] is S-type
 *   and belongs in the S-type region of bucket T[j-1]
 * 
 * @param T       Input text (byte alphabet)
 * @param SA      Suffix array (partially filled with LMS positions)
 * @param n       Length of input
 * @param K       Alphabet size (256)
 * @param t       Type array (packed bits)
 * @param bucket  Workspace for bucket computations
 */
static void induced_sort_byte(const uint8_t *T, int32_t *SA, size_t n, 
                              int K, const uint8_t *t, int32_t *bucket) {
    /*
     * PHASE 1: Induce L-type suffixes (left-to-right scan)
     * 
     * For each position SA[i] = j where j > 0:
     *   If T[j-1..] is L-type, place it at the front of bucket T[j-1]
     */
    get_buckets(T, n, K, bucket, 0);  /* Get bucket starts */
    
    for (size_t i = 0; i < n; i++) {
        int32_t j = SA[i];
        if (j > 0 && TYPE_BIT(t, j - 1) == LTYPE) {
            SA[bucket[T[j - 1]]++] = j - 1;
        }
    }
    
    /*
     * PHASE 2: Induce S-type suffixes (right-to-left scan)
     * 
     * For each position SA[i] = j where j > 0:
     *   If T[j-1..] is S-type, place it at the end of bucket T[j-1]
     */
    get_buckets(T, n, K, bucket, 1);  /* Get bucket ends */
    
    for (size_t i = n; i > 0; i--) {
        int32_t j = SA[i - 1];
        if (j > 0 && TYPE_BIT(t, j - 1) == STYPE) {
            SA[--bucket[T[j - 1]]] = j - 1;
        }
    }
}

/**
 * InducedSort for integer alphabet (used in recursive calls).
 */
static void induced_sort_int(const int32_t *T, int32_t *SA, size_t n,
                             int K, const uint8_t *t, int32_t *bucket) {
    get_buckets_int(T, n, K, bucket, 0);
    
    for (size_t i = 0; i < n; i++) {
        int32_t j = SA[i];
        if (j > 0 && TYPE_BIT(t, j - 1) == LTYPE) {
            SA[bucket[T[j - 1]]++] = j - 1;
        }
    }
    
    get_buckets_int(T, n, K, bucket, 1);
    
    for (size_t i = n; i > 0; i--) {
        int32_t j = SA[i - 1];
        if (j > 0 && TYPE_BIT(t, j - 1) == STYPE) {
            SA[--bucket[T[j - 1]]] = j - 1;
        }
    }
}

static int lms_equal(const uint8_t *T, size_t n, const uint8_t *t,
                     size_t i, size_t j) {
    if (i == j) return 1;
    
    size_t p = 0;
    while (1) {
        if (T[i + p] != T[j + p]) return 0;
        if (TYPE_BIT(t, i + p) != TYPE_BIT(t, j + p)) return 0;
        p++;
        
        int lms_i = IS_LMS(t, i + p);
        int lms_j = IS_LMS(t, j + p);
        
        if (lms_i && lms_j) return 1;  /* Both reached next LMS - equal */
        if (lms_i != lms_j) return 0;   /* One is LMS, other isn't - different */
        
        /* Safety check for end of string */
        if (i + p >= n || j + p >= n) return 0;
    }
}

static int sais_main(const uint8_t *T, int32_t *SA, size_t n, int K) {
    if (n == 0) return SAIS_OK;
    if (n == 1) {
        SA[0] = 0;
        return SAIS_OK;
    }
    
    size_t type_size = (n + 7) / 8;
    uint8_t *t = calloc(type_size, 1);
    if (!t) return SAIS_ERR_MEMORY;
    
    int32_t *bucket = malloc(K * sizeof(int32_t));
    if (!bucket) {
        free(t);
        return SAIS_ERR_MEMORY;
    }
    
    /*
     * Definition:
     * - Suffix T[i..] is S-type if T[i..] < T[i+1..] (lexicographically)
     * - Suffix T[i..] is L-type if T[i..] > T[i+1..]
     * 
     * Key insight: We can determine this in one right-to-left pass:
     * - Last suffix is always S-type (by convention)
     * - T[i..] is S-type if T[i] < T[i+1], or T[i] == T[i+1] and T[i+1..] is S-type
     * - Otherwise L-type
     */
    SET_STYPE(t, n - 1);  /* Last position is S-type */
    
    for (size_t i = n - 1; i > 0; i--) {
        if (T[i - 1] < T[i] || (T[i - 1] == T[i] && TYPE_BIT(t, i) == STYPE)) {
            SET_STYPE(t, i - 1);
        } else {
            SET_LTYPE(t, i - 1);
        }
    }
    
    /*
     * Find and bucket-sort LMS suffixes
     * 
     * LMS (Left-Most S-type) suffixes are S-type suffixes whose
     * predecessor is L-type. These are the "seeds" for induced sorting.
     */
    get_buckets(T, n, K, bucket, 1);  /* Get bucket ends */
    
    /* Initialize SA with empty slots */
    for (size_t i = 0; i < n; i++) {
        SA[i] = EMPTY_SLOT;
    }
    
    /* Place LMS suffixes at the end of their buckets */
    for (size_t i = 1; i < n; i++) {
        if (IS_LMS(t, i)) {
            SA[--bucket[T[i]]] = (int32_t)i;
        }
    }
    
    /* First induced sort - sort LMS substrings */
    induced_sort_byte(T, SA, n, K, t, bucket);
    
    /*
     * Compact sorted LMS suffixes and name them
     * 
     * We now have LMS substrings sorted. We need to:
     * 1. Extract just the LMS positions in sorted order
     * 2. Assign unique names to distinct LMS substrings
     * 3. Build the "reduced string" S1 of LMS names
     */
    
    /* Count LMS suffixes */
    size_t n1 = 0;
    for (size_t i = 1; i < n; i++) {
        if (IS_LMS(t, i)) n1++;
    }
    
    if (n1 == 0) {
        /* No LMS suffixes - edge case */
        free(bucket);
        free(t);
        return SAIS_OK;
    }
    
    /* Compact LMS suffixes to front of SA */
    size_t j = 0;
    for (size_t i = 0; i < n; i++) {
        if (SA[i] > 0 && IS_LMS(t, SA[i])) {
            SA[j++] = SA[i];
        }
    }
    
    /* 
     * Allocate separate buffer for names to avoid complex in-place indexing.
     * This uses extra memory but is correct and simpler.
     */
    int32_t *names = calloc(n, sizeof(int32_t));
    if (!names) {
        free(bucket);
        free(t);
        return SAIS_ERR_MEMORY;
    }
    
    /* Initialize names array */
    for (size_t i = 0; i < n; i++) {
        names[i] = EMPTY_SLOT;
    }
    
    /* Assign names to LMS substrings */
    int32_t name = 0;
    int32_t prev = -1;
    
    for (size_t i = 0; i < n1; i++) {
        int32_t pos = SA[i];
        int is_diff = 1;
        
        if (prev >= 0) {
            is_diff = !lms_equal(T, n, t, (size_t)prev, (size_t)pos);
        }
        
        if (is_diff) {
            name++;
            prev = pos;
        }
        
        /* Store name at the LMS position */
        names[pos] = name - 1;
    }
    
    /* Build reduced string S1 in the back of SA */
    int32_t *SA1 = SA + n - n1;
    j = 0;
    for (size_t i = 0; i < n; i++) {
        if (names[i] != EMPTY_SLOT) {
            SA1[j++] = names[i];
        }
    }
    
    free(names);
    
    /*
     * Recursion or direct construction
     * 
     * If all LMS names are unique, we can directly construct SA1.
     * Otherwise, recursively sort the reduced string S1.
     */
    if ((size_t)name < n1) {
        /* Not all names unique - need recursive call */
        
        /* Allocate type array for reduced string */
        size_t t1_size = (n1 + 7) / 8;
        uint8_t *t1 = calloc(t1_size, 1);
        if (!t1) {
            free(bucket);
            free(t);
            return SAIS_ERR_MEMORY;
        }
        
        int32_t *bucket1 = malloc(name * sizeof(int32_t));
        if (!bucket1) {
            free(t1);
            free(bucket);
            free(t);
            return SAIS_ERR_MEMORY;
        }
        
        /* Classify reduced string */
        if (n1 > 0) {
            SET_STYPE(t1, n1 - 1);
            for (size_t i = n1 - 1; i > 0; i--) {
                if (SA1[i - 1] < SA1[i] || 
                    (SA1[i - 1] == SA1[i] && TYPE_BIT(t1, i) == STYPE)) {
                    SET_STYPE(t1, i - 1);
                } else {
                    SET_LTYPE(t1, i - 1);
                }
            }
        }
        
        /* Get bucket ends and place LMS of reduced string */
        get_buckets_int(SA1, n1, name, bucket1, 1);
        
        int32_t *SA1_work = SA;  /* Use front of SA for recursive work */
        for (size_t i = 0; i < n1; i++) {
            SA1_work[i] = EMPTY_SLOT;
        }
        
        for (size_t i = 1; i < n1; i++) {
            if (IS_LMS(t1, i)) {
                SA1_work[--bucket1[SA1[i]]] = (int32_t)i;
            }
        }
        
        /* Induced sort on reduced string */
        induced_sort_int(SA1, SA1_work, n1, name, t1, bucket1);
        
        /* Copy result back */
        for (size_t i = 0; i < n1; i++) {
            SA1[i] = SA1_work[i];
        }
        
        free(bucket1);
        free(t1);
    } else {
        /* All names unique - directly compute SA1 */
        for (size_t i = 0; i < n1; i++) {
            SA[SA1[i]] = (int32_t)i;
        }
        for (size_t i = 0; i < n1; i++) {
            SA1[i] = SA[i];
        }
    }
    
    /*
     * Induce final SA from sorted LMS suffixes
     * 
     * Now SA1 contains the suffix array of the reduced string.
     * We need to map this back to positions in T and induce the full SA.
     */
    
    /* Get LMS positions in text order - need n1 slots */
    int32_t *LMS_pos = NULL;
    int lms_allocated = 0;
    
    if ((size_t)K >= n1) {
        /* Reuse bucket array since it's large enough */
        LMS_pos = bucket;
    } else {
        /* Need separate allocation */
        LMS_pos = malloc(n1 * sizeof(int32_t));
        if (!LMS_pos) {
            free(bucket);
            free(t);
            return SAIS_ERR_MEMORY;
        }
        lms_allocated = 1;
    }
    
    j = 0;
    for (size_t i = 1; i < n; i++) {
        if (IS_LMS(t, i)) {
            LMS_pos[j++] = (int32_t)i;
        }
    }
    
    /* Map SA1 indices back to text positions */
    for (size_t i = 0; i < n1; i++) {
        SA1[i] = LMS_pos[SA1[i]];
    }
    
    if (lms_allocated) {
        free(LMS_pos);
    }
    
    /* Clear SA and place sorted LMS suffixes */
    for (size_t i = 0; i < n - n1; i++) {
        SA[i] = EMPTY_SLOT;
    }
    
    /* Recompute bucket ends (bucket array is still valid) */
    get_buckets(T, n, K, bucket, 1);
    
    /* Place LMS suffixes in reverse order of SA1 */
    for (size_t i = n1; i > 0; i--) {
        int32_t pos = SA1[i - 1];
        SA1[i - 1] = EMPTY_SLOT;
        SA[--bucket[T[pos]]] = pos;
    }
    
    /* Final induced sort */
    induced_sort_byte(T, SA, n, K, t, bucket);
    
    free(bucket);
    free(t);
    
    return SAIS_OK;
}

int sais_build_sa(const uint8_t *T, int32_t *SA, size_t n) {
    if (!T || !SA) return SAIS_ERR_INPUT;
    if (n > (size_t)INT32_MAX) return SAIS_ERR_OVERFLOW;
    
    return sais_main(T, SA, n, SAIS_ALPHABET_SIZE);
}

int sais_build_sa_bounded(const uint8_t *T, int32_t *SA, size_t n,
                          size_t max_memory) {
    /* Estimate required memory */
    size_t required = sais_estimate_memory(n, 0);
    
    if (required > max_memory) {
        return SAIS_ERR_MEMORY;
    }
    
    return sais_build_sa(T, SA, n);
}

size_t sais_estimate_memory(size_t n, bool with_lcp) {
    /*
     * - SA: 4 * n bytes
     * - Type array: n / 8 bytes
     * - Bucket array: 256 * 4 = 1KB
     * - Recursion workspace: ~n/3 * 4 bytes (worst case)
     * - LCP (optional): 4 * n bytes
     * - Inverse SA for LCP: 4 * n bytes (temporary)
     */
    size_t base = 4 * n + (n + 7) / 8 + 1024 + (n / 3 + 1) * 4;
    
    if (with_lcp) {
        base += 4 * n + 4 * n;  /* LCP + inverse SA */
    }
    
    return base;
}

/**
 * Kasai's Algorithm for LCP array construction.
 * 
 * The key insight: LCP values for adjacent suffixes in text order
 * (not SA order) decrease by at most 1.
 * 
 * If LCP[ISA[i]] = h, then LCP[ISA[i+1]] >= h - 1
 * 
 */
int sais_build_lcp(const uint8_t *T, const int32_t *SA, int32_t *LCP, size_t n) {
    if (!T || !SA || !LCP) return SAIS_ERR_INPUT;
    if (n == 0) return SAIS_OK;
    if (n == 1) {
        LCP[0] = 0;
        return SAIS_OK;
    }
    
    /* Build inverse suffix array: ISA[SA[i]] = i */
    int32_t *ISA = malloc(n * sizeof(int32_t));
    if (!ISA) return SAIS_ERR_MEMORY;
    
    for (size_t i = 0; i < n; i++) {
        ISA[SA[i]] = (int32_t)i;
    }
    
    /*
     * Kasai's algorithm:
     * Process suffixes in text order (i = 0, 1, 2, ...)
     * Use the h-1 property to avoid redundant comparisons
     */
    int32_t h = 0;
    
    for (size_t i = 0; i < n; i++) {
        int32_t k = ISA[i];  /* Rank of suffix starting at position i */
        
        if (k > 0) {
            /* Compare with suffix at rank k-1 */
            int32_t j = SA[k - 1];  /* Position of lexicographically previous suffix */
            
            /* Extend match as far as possible */
            while (i + h < n && j + h < (int32_t)n && T[i + h] == T[j + h]) {
                h++;
            }
            
            LCP[k] = h;
        } else {
            LCP[k] = 0;  /* First suffix in sorted order has no predecessor */
        }
        
        /* h decreases by at most 1 for next iteration */
        if (h > 0) h--;
    }
    
    free(ISA);
    return SAIS_OK;
}

/**
 * PLCP-based LCP construction.
 * Builds Permuted LCP array first, which has better cache locality.
 */
int sais_build_lcp_plcp(const uint8_t *T, const int32_t *SA,
                        int32_t *LCP, size_t n) {
    if (!T || !SA || !LCP) return SAIS_ERR_INPUT;
    if (n == 0) return SAIS_OK;
    if (n == 1) {
        LCP[0] = 0;
        return SAIS_OK;
    }
    
    /* Build inverse suffix array */
    int32_t *ISA = malloc(n * sizeof(int32_t));
    if (!ISA) return SAIS_ERR_MEMORY;
    
    for (size_t i = 0; i < n; i++) {
        ISA[SA[i]] = (int32_t)i;
    }
    
    /* Build Phi array: Phi[SA[i]] = SA[i-1] */
    int32_t *Phi = malloc(n * sizeof(int32_t));
    if (!Phi) {
        free(ISA);
        return SAIS_ERR_MEMORY;
    }
    
    Phi[SA[0]] = -1;  /* No predecessor for first suffix */
    for (size_t i = 1; i < n; i++) {
        Phi[SA[i]] = SA[i - 1];
    }
    
    /* Build PLCP: PLCP[i] = LCP[ISA[i]] = lcp(T[i..], T[Phi[i]..]) */
    int32_t *PLCP = LCP;  /* Reuse LCP array for PLCP temporarily */
    int32_t h = 0;
    
    for (size_t i = 0; i < n; i++) {
        if (Phi[i] >= 0) {
            int32_t j = Phi[i];
            while (i + h < n && j + h < (int32_t)n && T[i + h] == T[j + h]) {
                h++;
            }
            PLCP[i] = h;
        } else {
            PLCP[i] = 0;
        }
        
        if (h > 0) h--;
    }
    
    /* Convert PLCP to LCP: LCP[i] = PLCP[SA[i]] */
    /* Need temp storage since we're overwriting */
    int32_t *temp = Phi;  /* Reuse Phi as temp */
    for (size_t i = 0; i < n; i++) {
        temp[i] = PLCP[SA[i]];
    }
    
    memcpy(LCP, temp, n * sizeof(int32_t));
    
    free(Phi);
    free(ISA);
    
    return SAIS_OK;
}

int sais_build_bwt(const uint8_t *T, const int32_t *SA, uint8_t *BWT,
                   size_t n, uint32_t *primary_index) {
    if (!T || !SA || !BWT || !primary_index) return SAIS_ERR_INPUT;
    
    *primary_index = 0;
    
    for (size_t i = 0; i < n; i++) {
        if (SA[i] == 0) {
            *primary_index = (uint32_t)i;
            BWT[i] = T[n - 1];
        } else {
            BWT[i] = T[SA[i] - 1];
        }
    }
    
    return SAIS_OK;
}

typedef struct {
    pthread_t *threads;
    uint32_t num_threads;
    size_t per_thread_memory;
    volatile int shutdown;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} thread_pool_t;

static thread_pool_t *g_pool = NULL;

int sais_thread_pool_init(const sais_thread_config_t *config) {
    if (g_pool) return SAIS_OK;  /* Already initialized */
    
    g_pool = calloc(1, sizeof(thread_pool_t));
    if (!g_pool) return SAIS_ERR_MEMORY;
    
    g_pool->num_threads = config->num_threads;
    if (g_pool->num_threads == 0) {
        g_pool->num_threads = (uint32_t)sysconf(_SC_NPROCESSORS_ONLN);
        if (g_pool->num_threads == 0) g_pool->num_threads = 4;
    }
    
    g_pool->per_thread_memory = config->per_thread_memory;
    if (g_pool->per_thread_memory == 0) {
        g_pool->per_thread_memory = SAIS_MAX_RSS_BYTES / g_pool->num_threads;
    }
    
    pthread_mutex_init(&g_pool->lock, NULL);
    pthread_cond_init(&g_pool->cond, NULL);
    
    return SAIS_OK;
}

void sais_thread_pool_shutdown(void) {
    if (!g_pool) return;
    
    g_pool->shutdown = 1;
    pthread_mutex_destroy(&g_pool->lock);
    pthread_cond_destroy(&g_pool->cond);
    free(g_pool->threads);
    free(g_pool);
    g_pool = NULL;
}

typedef struct {
    const sais_solid_group_t *group;
    sais_result_t *result;
    bool build_lcp;
    int error;
} worker_arg_t;

static void *worker_thread(void *arg) {
    worker_arg_t *warg = (worker_arg_t *)arg;
    const sais_solid_group_t *group = warg->group;
    sais_result_t *result = warg->result;
    
    size_t n = group->size;
    
    result->SA = malloc(n * sizeof(int32_t));
    if (!result->SA) {
        warg->error = SAIS_ERR_MEMORY;
        return NULL;
    }
    
    warg->error = sais_build_sa(group->data, result->SA, n);
    if (warg->error != SAIS_OK) {
        free(result->SA);
        result->SA = NULL;
        return NULL;
    }
    
    if (warg->build_lcp) {
        result->LCP = malloc(n * sizeof(int32_t));
        if (!result->LCP) {
            free(result->SA);
            result->SA = NULL;
            warg->error = SAIS_ERR_MEMORY;
            return NULL;
        }
        
        warg->error = sais_build_lcp(group->data, result->SA, result->LCP, n);
        if (warg->error != SAIS_OK) {
            free(result->SA);
            free(result->LCP);
            result->SA = NULL;
            result->LCP = NULL;
            return NULL;
        }
    }
    
    result->BWT = malloc(n);
    if (!result->BWT) {
        free(result->SA);
        free(result->LCP);
        result->SA = NULL;
        result->LCP = NULL;
        warg->error = SAIS_ERR_MEMORY;
        return NULL;
    }
    
    sais_build_bwt(group->data, result->SA, result->BWT, n, &result->primary_index);
    
    return NULL;
}

int sais_process_groups_parallel(const sais_solid_group_t *groups,
                                  size_t num_groups,
                                  sais_result_t *results,
                                  bool build_lcp) {
    if (!groups || !results || num_groups == 0) {
        return SAIS_ERR_INPUT;
    }
    
    if (!g_pool) {
        sais_thread_config_t config = {0, 0, 0};
        int err = sais_thread_pool_init(&config);
        if (err != SAIS_OK) return err;
    }
    
    pthread_t *threads = malloc(num_groups * sizeof(pthread_t));
    worker_arg_t *args = malloc(num_groups * sizeof(worker_arg_t));
    
    if (!threads || !args) {
        free(threads);
        free(args);
        return SAIS_ERR_MEMORY;
    }
    
    for (size_t i = 0; i < num_groups; i++) {
        args[i].group = &groups[i];
        args[i].result = &results[i];
        args[i].build_lcp = build_lcp;
        args[i].error = SAIS_OK;
        
        memset(&results[i], 0, sizeof(sais_result_t));
        
        int err = pthread_create(&threads[i], NULL, worker_thread, &args[i]);
        if (err != 0) {
            for (size_t j = 0; j < i; j++) {
                pthread_cancel(threads[j]);
            }
            free(threads);
            free(args);
            return SAIS_ERR_MEMORY;
        }
    }
    
    int first_error = SAIS_OK;
    for (size_t i = 0; i < num_groups; i++) {
        pthread_join(threads[i], NULL);
        if (args[i].error != SAIS_OK && first_error == SAIS_OK) {
            first_error = args[i].error;
        }
    }
    
    free(threads);
    free(args);
    
    return first_error;
}

bool sais_validate_sa(const uint8_t *T, const int32_t *SA, size_t n) {
    if (n <= 1) return true;
    
    uint8_t *seen = calloc((n + 7) / 8, 1);
    if (!seen) return false;
    
    for (size_t i = 0; i < n; i++) {
        if (SA[i] < 0 || (size_t)SA[i] >= n) {
            free(seen);
            return false;
        }
        if (seen[SA[i] >> 3] & (1 << (SA[i] & 7))) {
            free(seen);
            return false;  /* Duplicate */
        }
        seen[SA[i] >> 3] |= (1 << (SA[i] & 7));
    }
    free(seen);
    
    for (size_t i = 1; i < n; i++) {
        int32_t a = SA[i - 1];
        int32_t b = SA[i];
        
        /* Compare T[a..] with T[b..] */
        for (size_t k = 0; k < n; k++) {
            if ((size_t)(a + k) >= n && (size_t)(b + k) >= n) break;
            if ((size_t)(a + k) >= n) break;  /* a is shorter, should be first - OK */
            if ((size_t)(b + k) >= n) return false;  /* b is shorter but comes after - ERROR */
            
            if (T[a + k] < T[b + k]) break;  /* Correct order */
            if (T[a + k] > T[b + k]) return false;  /* Wrong order */
        }
    }
    
    return true;
}

bool sais_validate_lcp(const uint8_t *T, const int32_t *SA,
                       const int32_t *LCP, size_t n) {
    if (n <= 1) return true;
    
    for (size_t i = 1; i < n; i++) {
        int32_t a = SA[i - 1];
        int32_t b = SA[i];
        
        /* Compute actual LCP */
        int32_t h = 0;
        while ((size_t)(a + h) < n && (size_t)(b + h) < n && T[a + h] == T[b + h]) {
            h++;
        }
        
        if (LCP[i] != h) {
            return false;
        }
    }
    
    return true;
}
