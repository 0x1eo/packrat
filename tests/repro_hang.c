#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "sais.h"

// 40 MB size to match the rough size of the ISO causing issues
#define TEST_SIZE (40 * 1024 * 1024)

int main() {
    printf("Allocating %d MB...\n", TEST_SIZE / 1024 / 1024);
    
    uint8_t *input = malloc(TEST_SIZE);
    int32_t *SA = malloc(TEST_SIZE * sizeof(int32_t));
    
    if (!input || !SA) {
        printf("Failed to allocate memory\n");
        return 1;
    }
    
    // Fill with repetitive data which can be problematic for some SA algorithms
    printf("Filling buffer...\n");
    for (size_t i = 0; i < TEST_SIZE; i++) {
        input[i] = (i % 256); // Simple pattern
        if (i % 100 == 0) input[i] = 0; // Add some zeros
    }
    // Ensure unique terminator if needed, but standard SA-IS handles raw bytes usually.
    // However, some implementations expect a sentinel. The current one takes raw bytes.
    
    printf("Starting SA construction...\n");
    clock_t start = clock();
    
    int err = sais_build_sa(input, SA, TEST_SIZE);
    
    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    
    if (err != 0) {
        printf("SA-IS failed: %d\n", err);
        return 1;
    }
    
    printf("Done in %.2f seconds\n", time_spent);
    
    free(input);
    free(SA);
    return 0;
}
