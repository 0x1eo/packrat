/* packrat command line interface */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include "packrat.h"
#include "archive.h"
#include "archive_v3.h"

static void print_usage(const char *prog) {
    fprintf(stderr, "packrat - High-ratio text compression\n\n");
    fprintf(stderr, "Single file:\n");
    fprintf(stderr, "  %s -c <input> [output]  Compress file\n", prog);
    fprintf(stderr, "  %s -d <input> [output]  Decompress file\n", prog);
    fprintf(stderr, "  %s -t <file>            Test integrity\n", prog);
    fprintf(stderr, "  %s -i <file>            Show file info\n", prog);
    fprintf(stderr, "\nArchive (multiple files):\n");
    fprintf(stderr, "  %s -a <archive> <files...>          Create archive\n", prog);
    fprintf(stderr, "  %s -a --solid <archive> <files...>  Create solid archive (better ratio)\n", prog);
    fprintf(stderr, "  %s -l <archive>                     List archive contents\n", prog);
    fprintf(stderr, "  %s -x <archive> [outdir]            Extract archive\n", prog);
    fprintf(stderr, "\nGeneral:\n");
    fprintf(stderr, "  %s -h                   Show this help\n", prog);
}

static const char* error_string(int code) {
    switch (code) {
        case PRT_OK:              return "Success";
        case PRT_ERR_MEMORY:      return "Memory allocation failed";
        case PRT_ERR_FORMAT:      return "Invalid file format";
        case PRT_ERR_FILE:        return "File I/O error";
        case PRT_ERR_CORRUPT:     return "Corrupted data";
        case PRT_ERR_PATH_TOO_LONG: return "Path too long";
        default:                  return "Unknown error";
    }
}

static long get_file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fclose(f);
    return size;
}

static int is_directory(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return S_ISDIR(st.st_mode);
}

static int cmd_compress(const char *input, const char *output) {
    char auto_output[4096];
    const char *actual_output = output;
    
    if (!output) {
        snprintf(auto_output, sizeof(auto_output), "%s.prt", input);
        actual_output = auto_output;
    }
    
    printf("Compressing: %s -> %s\n", input, actual_output);
    
    clock_t start = clock();
    int result = prt_compress_file(input, actual_output);
    clock_t end = clock();
    
    if (result != PRT_OK) {
        fprintf(stderr, "Error: %s\n", error_string(result));
        return 1;
    }
    
    long input_size = get_file_size(input);
    long output_size = get_file_size(actual_output);
    double ratio = (input_size > 0) ? (double)output_size / input_size * 100.0 : 0;
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("Original:   %ld bytes\n", input_size);
    printf("Compressed: %ld bytes\n", output_size);
    printf("Ratio:      %.1f%% (%.2fx)\n", ratio, (ratio > 0) ? 100.0 / ratio : 0);
    printf("Time:       %.3f seconds\n", elapsed);
    
    return 0;
}

static int cmd_decompress(const char *input, const char *output) {
    char auto_output[4096];
    const char *actual_output = output;
    
    if (!output) {
        strncpy(auto_output, input, sizeof(auto_output) - 1);
        auto_output[sizeof(auto_output) - 1] = '\0';
        size_t len = strlen(auto_output);
        if (len > 4 && strcmp(auto_output + len - 4, ".prt") == 0) {
            auto_output[len - 4] = '\0';
        } else {
            strncat(auto_output, ".out", sizeof(auto_output) - len - 1);
        }
        actual_output = auto_output;
    }
    
    printf("Decompressing: %s -> %s\n", input, actual_output);
    
    clock_t start = clock();
    int result = prt_decompress_file(input, actual_output);
    clock_t end = clock();
    
    if (result != PRT_OK) {
        fprintf(stderr, "Error: %s\n", error_string(result));
        return 1;
    }
    
    long input_size = get_file_size(input);
    long output_size = get_file_size(actual_output);
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    printf("Compressed: %ld bytes\n", input_size);
    printf("Original:   %ld bytes\n", output_size);
    printf("Time:       %.3f seconds\n", elapsed);
    
    return 0;
}

static int cmd_info(const char *input) {
    FILE *f = fopen(input, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file\n");
        return 1;
    }
    
    uint8_t header[PRT_HEADER_SIZE];
    size_t read_bytes = fread(header, 1, PRT_HEADER_SIZE, f);
    fclose(f);
    
    if (read_bytes < 20) {
        fprintf(stderr, "Error: Invalid file format\n");
        return 1;
    }
    
    if (memcmp(header, "PRTA", 4) == 0) {
        prt_archive_t *archive = prt_archive_open(input);
        if (archive) {
            prt_archive_list(archive);
            prt_archive_close(archive);
            return 0;
        }
    }
    
    printf("File: %s\n", input);
    printf("Format: Single file (version %d)\n", header[3]);
    
    size_t original_size = prt_get_original_size(header, PRT_HEADER_SIZE);
    printf("Original size: %zu bytes\n", original_size);
    
    long compressed_size = get_file_size(input);
    if (compressed_size > 0 && original_size > 0) {
        double ratio = (double)compressed_size / original_size * 100.0;
        printf("Compressed size: %ld bytes (%.1f%%)\n", compressed_size, ratio);
    }
    
    return 0;
}

static int cmd_test(const char *input) {
    printf("Testing: %s\n", input);
    
    FILE *f = fopen(input, "rb");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file\n");
        return 1;
    }
    
    fseek(f, 0, SEEK_END);
    size_t input_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    uint8_t *compressed = malloc(input_size);
    if (!compressed) {
        fclose(f);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return 1;
    }
    
    fread(compressed, 1, input_size, f);
    fclose(f);
    
    size_t original_size = prt_get_original_size(compressed, input_size);
    if (original_size == 0 && input_size > PRT_HEADER_SIZE) {
        free(compressed);
        fprintf(stderr, "Error: Invalid file format\n");
        return 1;
    }
    
    uint8_t *decompressed = malloc(original_size + 1);
    if (!decompressed && original_size > 0) {
        free(compressed);
        fprintf(stderr, "Error: Memory allocation failed\n");
        return 1;
    }
    
    size_t output_size = original_size;
    int result = prt_decompress(compressed, input_size, decompressed, &output_size);
    
    free(compressed);
    free(decompressed);
    
    if (result != PRT_OK) {
        fprintf(stderr, "FAILED: %s\n", error_string(result));
        return 1;
    }
    
    printf("OK - %zu bytes compressed, %zu bytes original\n", input_size, original_size);
    return 0;
}

static int cmd_archive_create(const char *archive_path, int file_count, char **files) {
    printf("Creating archive: %s\n", archive_path);
    
    clock_t start = clock();
    
    prt_archive_t *archive = prt_archive_create(archive_path);
    if (!archive) {
        fprintf(stderr, "Error: Cannot create archive\n");
        return 1;
    }
    
    int added = 0;
    for (int i = 0; i < file_count; i++) {
        const char *path = files[i];
        
        if (is_directory(path)) {
            printf("  Adding directory: %s/\n", path);
            int result = prt_archive_add_dir(archive, path, NULL);
            if (result != PRT_OK) {
                fprintf(stderr, "Error adding %s: %s\n", path, error_string(result));
                prt_archive_close(archive);
                return 1;
            }
        } else {
            printf("  Adding: %s\n", path);
            int result = prt_archive_add_file(archive, path, NULL);
            if (result != PRT_OK) {
                fprintf(stderr, "Error adding %s: %s\n", path, error_string(result));
                prt_archive_close(archive);
                return 1;
            }
            added++;
        }
    }
    
    int result = prt_archive_finalize(archive);
    if (result != PRT_OK) {
        fprintf(stderr, "Error finalizing archive: %s\n", error_string(result));
        prt_archive_close(archive);
        return 1;
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    uint32_t total_files = prt_archive_file_count(archive);
    long archive_size = get_file_size(archive_path);
    
    printf("\nArchive created: %u files, %ld bytes\n", total_files, archive_size);
    printf("Time: %.3f seconds\n", elapsed);
    
    prt_archive_close(archive);
    return 0;
}

static int cmd_solid_archive_create(const char *archive_path, int file_count, char **files) {
    printf("Creating solid archive: %s\n", archive_path);
    printf("(Files will be grouped by type for better compression)\n\n");
    
    clock_t start = clock();
    
    prt_solid_archive_t *archive = prt_solid_archive_create(archive_path);
    if (!archive) {
        fprintf(stderr, "Error: Cannot create archive\n");
        return 1;
    }
    
    for (int i = 0; i < file_count; i++) {
        const char *path = files[i];
        
        if (is_directory(path)) {
            printf("  Scanning: %s/\n", path);
            int result = prt_solid_archive_add_dir(archive, path, NULL);
            if (result != PRT_OK) {
                fprintf(stderr, "Error adding %s: %s\n", path, error_string(result));
                prt_solid_archive_close(archive);
                return 1;
            }
        } else {
            int result = prt_solid_archive_add_file(archive, path, NULL);
            if (result != PRT_OK) {
                fprintf(stderr, "Error adding %s: %s\n", path, error_string(result));
                prt_solid_archive_close(archive);
                return 1;
            }
        }
    }
    
    printf("\nCompressing solid blocks:\n");
    int result = prt_solid_archive_finalize(archive);
    if (result != PRT_OK) {
        fprintf(stderr, "Error finalizing archive: %s\n", error_string(result));
        prt_solid_archive_close(archive);
        return 1;
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    long archive_size = get_file_size(archive_path);
    
    printf("\nSolid archive created: %ld bytes\n", archive_size);
    printf("Time: %.3f seconds\n", elapsed);
    
    prt_solid_archive_close(archive);
    return 0;
}

static int get_archive_version(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    
    uint8_t magic[4];
    if (fread(magic, 1, 4, f) != 4) {
        fclose(f);
        return 0;
    }
    fclose(f);
    
    if (memcmp(magic, "PRT\x03", 4) == 0) return 3;
    if (memcmp(magic, "PRTA", 4) == 0) return 2;
    return 0;
}

static int cmd_archive_list(const char *archive_path) {
    int version = get_archive_version(archive_path);
    
    if (version == 3) {
        prt_solid_archive_t *archive = prt_solid_archive_open(archive_path);
        if (!archive) {
            fprintf(stderr, "Error: Cannot open archive\n");
            return 1;
        }
        prt_solid_archive_list(archive);
        prt_solid_archive_close(archive);
    } else if (version == 2) {
        prt_archive_t *archive = prt_archive_open(archive_path);
        if (!archive) {
            fprintf(stderr, "Error: Cannot open archive\n");
            return 1;
        }
        prt_archive_list(archive);
        prt_archive_close(archive);
    } else {
        fprintf(stderr, "Error: Unknown archive format\n");
        return 1;
    }
    
    return 0;
}

static int cmd_archive_extract(const char *archive_path, const char *output_dir) {
    int version = get_archive_version(archive_path);
    
    printf("Extracting: %s\n", archive_path);
    if (output_dir) {
        printf("Output directory: %s\n", output_dir);
    }
    printf("\n");
    
    clock_t start = clock();
    int result;
    uint32_t file_count;
    
    if (version == 3) {
        prt_solid_archive_t *archive = prt_solid_archive_open(archive_path);
        if (!archive) {
            fprintf(stderr, "Error: Cannot open archive\n");
            return 1;
        }
        
        result = prt_solid_archive_extract_all(archive, output_dir);
        file_count = prt_solid_archive_file_count(archive);
        prt_solid_archive_close(archive);
    } else if (version == 2) {
        prt_archive_t *archive = prt_archive_open(archive_path);
        if (!archive) {
            fprintf(stderr, "Error: Cannot open archive\n");
            return 1;
        }
        
        result = prt_archive_extract_all(archive, output_dir);
        file_count = prt_archive_file_count(archive);
        prt_archive_close(archive);
    } else {
        fprintf(stderr, "Error: Unknown archive format\n");
        return 1;
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    
    if (result != PRT_OK) {
        fprintf(stderr, "Error: %s\n", error_string(result));
        return 1;
    }
    
    printf("\nExtracted %u files in %.3f seconds\n", file_count, elapsed);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }
    
    if (strcmp(argv[1], "-c") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: -c requires an input file\n");
            return 1;
        }
        return cmd_compress(argv[2], argc > 3 ? argv[3] : NULL);
    }
    
    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: -d requires an input file\n");
            return 1;
        }
        return cmd_decompress(argv[2], argc > 3 ? argv[3] : NULL);
    }
    
    if (strcmp(argv[1], "-t") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: -t requires a file\n");
            return 1;
        }
        return cmd_test(argv[2]);
    }
    
    if (strcmp(argv[1], "-i") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: -i requires a file\n");
            return 1;
        }
        return cmd_info(argv[2]);
    }
    
    if (strcmp(argv[1], "-a") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Error: -a requires archive name and files\n");
            return 1;
        }
        
        if (strcmp(argv[2], "--solid") == 0) {
            if (argc < 5) {
                fprintf(stderr, "Error: -a --solid requires archive name and files\n");
                return 1;
            }
            return cmd_solid_archive_create(argv[3], argc - 4, &argv[4]);
        }
        
        return cmd_archive_create(argv[2], argc - 3, &argv[3]);
    }
    
    if (strcmp(argv[1], "-l") == 0) {
        if (argc != 3) {
            fprintf(stderr, "Error: -l requires an archive file\n");
            return 1;
        }
        return cmd_archive_list(argv[2]);
    }
    
    if (strcmp(argv[1], "-x") == 0) {
        if (argc < 3) {
            fprintf(stderr, "Error: -x requires an archive file\n");
            return 1;
        }
        return cmd_archive_extract(argv[2], argc > 3 ? argv[3] : NULL);
    }
    
    fprintf(stderr, "Error: Unknown option '%s'\n", argv[1]);
    print_usage(argv[0]);
    return 1;
}
