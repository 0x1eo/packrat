#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sais.h"
#include "bwt.h"

/* Test with small input first */
int main() {
    /* Test progressively larger sizes including 8KB */
    size_t test_sizes[] = {20, 100, 1000, 8193, 10000, 100000};
    int num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    for (int t = 0; t < num_tests; t++) {
        size_t size = test_sizes[t];
        
        uint8_t *input = malloc(size);
        uint8_t *output = malloc(size);
        uint8_t *decoded = malloc(size);
        int32_t *SA = malloc(size * sizeof(int32_t));
        
        for (size_t i = 0; i < size - 1; i++) {
            input[i] = "abc"[i % 3];
        }
        input[size - 1] = '\n';
        
        printf("Test: size=%zu\n", size);
        
        int err = sais_build_sa(input, SA, size);
        if (err != 0) {
            printf("  SA-IS failed: %d\n", err);
            return 1;
        }
        
        if (!sais_validate_sa(input, SA, size)) {
            printf("  SA validation: FAIL\n");
            return 1;
        }
        
        uint32_t primary;
        sais_build_bwt(input, SA, output, size, &primary);
        bwt_decode(output, decoded, size, primary);
        
        int diff_count = 0;
        for (size_t i = 0; i < size; i++) {
            if (input[i] != decoded[i]) {
                if (diff_count < 5) {
                    printf("  Diff at %zu: input=0x%02x decoded=0x%02x\n",
                           i, input[i], decoded[i]);
                }
                diff_count++;
            }
        }
        
        if (diff_count != 0) {
            printf("  Roundtrip: FAIL (%d differences)\n", diff_count);
            free(input); free(output); free(decoded); free(SA);
            return 1;
        }
        
        printf("  PASS\n");
        
        free(input);
        free(output);
        free(decoded);
        free(SA);
    }
    
    printf("All tests passed!\n");
    return 0;
}
