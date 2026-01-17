/* Multi-file archive with compression */

#include "archive.h"
#include "packrat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

struct prt_archive {
    FILE *file;
    char *path;
    int mode;
    uint32_t file_count;
    prt_file_entry_t *entries;
    size_t entries_capacity;
    uint64_t data_offset;
};

static const char *store_extensions[] = {
    /* Already compressed archives */
    ".zip", ".gz", ".bz2", ".xz", ".7z", ".rar", ".tar.gz", ".tgz",
    /* Images */
    ".jpg", ".jpeg", ".png", ".gif", ".webp", ".ico", ".svg",
    /* Audio/Video */
    ".mp3", ".mp4", ".avi", ".mkv", ".webm", ".ogg", ".flac",
    /* Documents (usually compressed internally) */
    ".pdf", ".docx", ".xlsx", ".pptx", ".odt",
    /* Other compressed */
    ".woff", ".woff2", ".eot",
    NULL
};

/* Minimum file size to attempt compression (bytes) */
#define MIN_COMPRESS_SIZE 512

static void write_u32_le(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

static uint32_t read_u32_le(const uint8_t *buf) {
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static void write_u64_le(uint8_t *buf, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        buf[i] = (val >> (i * 8)) & 0xFF;
    }
}

static uint64_t read_u64_le(const uint8_t *buf) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val |= ((uint64_t)buf[i]) << (i * 8);
    }
    return val;
}

static int should_store(const char *path, size_t file_size) {
    if (file_size < MIN_COMPRESS_SIZE) return 1;
    
    /* Check extension */
    const char *ext = strrchr(path, '.');
    if (!ext) return 0;
    
    /* Convert to lowercase for comparison */
    char ext_lower[32];
    size_t i;
    for (i = 0; ext[i] && i < sizeof(ext_lower) - 1; i++) {
        ext_lower[i] = tolower((unsigned char)ext[i]);
    }
    ext_lower[i] = '\0';
    
    /* Check against known compressed extensions */
    for (const char **e = store_extensions; *e; e++) {
        if (strcmp(ext_lower, *e) == 0) {
            return 1;
        }
    }
    
    return 0;
}

static int mkdir_p(const char *path) {
    char tmp[PRT_MAX_PATH];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (len > 0 && tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    return mkdir(tmp, 0755);
}

static void get_dirname(const char *path, char *dir) {
    const char *last_slash = strrchr(path, '/');
    if (last_slash) {
        size_t len = last_slash - path;
        memcpy(dir, path, len);
        dir[len] = '\0';
    } else {
        dir[0] = '\0';
    }
}

/* Safely build a path, returning error code if truncated */
static int build_path(char *dest, size_t dest_size, const char *dir, const char *file) {
    size_t needed = snprintf(dest, dest_size, "%s/%s", dir, file);
    if (needed >= dest_size) {
        return PRT_ERR_PATH_TOO_LONG;
    }
    return PRT_OK;
}

prt_archive_t* prt_archive_create(const char *path) {
    prt_archive_t *archive = calloc(1, sizeof(prt_archive_t));
    if (!archive) return NULL;
    
    archive->file = fopen(path, "wb");
    if (!archive->file) {
        free(archive);
        return NULL;
    }
    
    archive->path = strdup(path);
    archive->mode = 1;  /* Write mode */
    archive->file_count = 0;
    archive->entries_capacity = 64;
    archive->entries = calloc(archive->entries_capacity, sizeof(prt_file_entry_t));
    
    if (!archive->entries) {
        fclose(archive->file);
        free(archive->path);
        free(archive);
        return NULL;
    }
    
    /* Write placeholder header */
    uint8_t header[PRT_ARCHIVE_HEADER_SIZE] = {0};
    memcpy(header, PRT_ARCHIVE_MAGIC, 4);
    fwrite(header, 1, PRT_ARCHIVE_HEADER_SIZE, archive->file);
    
    archive->data_offset = PRT_ARCHIVE_HEADER_SIZE;
    
    return archive;
}

prt_archive_t* prt_archive_open(const char *path) {
    prt_archive_t *archive = calloc(1, sizeof(prt_archive_t));
    if (!archive) return NULL;
    
    archive->file = fopen(path, "rb");
    if (!archive->file) {
        free(archive);
        return NULL;
    }
    
    archive->path = strdup(path);
    archive->mode = 0;
    
    uint8_t header[PRT_ARCHIVE_HEADER_SIZE];
    if (fread(header, 1, PRT_ARCHIVE_HEADER_SIZE, archive->file) != PRT_ARCHIVE_HEADER_SIZE) {
        fclose(archive->file);
        free(archive->path);
        free(archive);
        return NULL;
    }
    
    if (memcmp(header, PRT_ARCHIVE_MAGIC, 4) != 0) {
        fclose(archive->file);
        free(archive->path);
        free(archive);
        return NULL;
    }
    
    archive->file_count = read_u32_le(header + 8);
    uint64_t table_offset = read_u64_le(header + 12);
    
    archive->entries = calloc(archive->file_count, sizeof(prt_file_entry_t));
    if (!archive->entries && archive->file_count > 0) {
        fclose(archive->file);
        free(archive->path);
        free(archive);
        return NULL;
    }
    
    fseek(archive->file, table_offset, SEEK_SET);
    
    for (uint32_t i = 0; i < archive->file_count; i++) {
        /* Read path length + path */
        uint16_t path_len;
        if (fread(&path_len, 2, 1, archive->file) != 1) break;
        if (path_len >= PRT_MAX_PATH) path_len = PRT_MAX_PATH - 1;
        
        if (fread(archive->entries[i].path, 1, path_len, archive->file) != path_len) break;
        archive->entries[i].path[path_len] = '\0';
        
        /* Read sizes, offset, and flags */
        uint8_t entry_data[17];  /* 4 + 4 + 8 + 1 */
        if (fread(entry_data, 1, 17, archive->file) != 17) break;
        
        archive->entries[i].original_size = read_u32_le(entry_data);
        archive->entries[i].compressed_size = read_u32_le(entry_data + 4);
        archive->entries[i].offset = read_u64_le(entry_data + 8);
        archive->entries[i].flags = entry_data[16];
    }
    
    return archive;
}

void prt_archive_close(prt_archive_t *archive) {
    if (!archive) return;
    
    if (archive->file) fclose(archive->file);
    free(archive->path);
    free(archive->entries);
    free(archive);
}

int prt_archive_add_file(prt_archive_t *archive, const char *file_path,
                         const char *archive_path) {
    if (!archive || archive->mode != 1) return PRT_ERR_FILE;
    
    FILE *fin = fopen(file_path, "rb");
    if (!fin) return PRT_ERR_FILE;
    
    fseek(fin, 0, SEEK_END);
    size_t file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);
    
    uint8_t *data = malloc(file_size > 0 ? file_size : 1);
    if (!data) {
        fclose(fin);
        return PRT_ERR_MEMORY;
    }
    
    if (file_size > 0 && fread(data, 1, file_size, fin) != file_size) {
        free(data);
        fclose(fin);
        return PRT_ERR_FILE;
    }
    fclose(fin);
    
    const char *store_path = archive_path;
    if (!store_path) {
        store_path = strrchr(file_path, '/');
        store_path = store_path ? store_path + 1 : file_path;
    }
    
    if (strlen(store_path) >= PRT_MAX_PATH) {
        free(data);
        return PRT_ERR_PATH_TOO_LONG;
    }
    
    uint8_t *output_data;
    size_t output_size;
    uint8_t flags = 0;
    
    if (should_store(file_path, file_size)) {
        output_data = data;
        output_size = file_size;
        flags = PRT_FLAG_STORED;
    } else {
        size_t compressed_max = file_size + PRT_HEADER_SIZE + 1024;
        uint8_t *compressed = malloc(compressed_max);
        if (!compressed) {
            free(data);
            return PRT_ERR_MEMORY;
        }
        
        size_t compressed_size;
        int result = prt_compress(data, file_size, compressed, &compressed_size);
        
        if (result != PRT_OK || compressed_size >= file_size) {
            free(compressed);
            output_data = data;
            output_size = file_size;
            flags = PRT_FLAG_STORED;
        } else {
            free(data);
            output_data = compressed;
            output_size = compressed_size;
            flags = 0;
        }
    }
    
    if (archive->file_count >= archive->entries_capacity) {
        archive->entries_capacity *= 2;
        archive->entries = realloc(archive->entries, 
                                   archive->entries_capacity * sizeof(prt_file_entry_t));
        if (!archive->entries) {
            free(output_data);
            return PRT_ERR_MEMORY;
        }
    }
    
    prt_file_entry_t *entry = &archive->entries[archive->file_count];
    strncpy(entry->path, store_path, PRT_MAX_PATH - 1);
    entry->path[PRT_MAX_PATH - 1] = '\0';
    entry->original_size = file_size;
    entry->compressed_size = output_size;
    entry->offset = archive->data_offset;
    entry->flags = flags;
    
    if (output_size > 0) {
        fwrite(output_data, 1, output_size, archive->file);
    }
    archive->data_offset += output_size;
    archive->file_count++;
    
    free(output_data);
    return PRT_OK;
}

int prt_archive_add_dir(prt_archive_t *archive, const char *dir_path,
                        const char *base_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return PRT_ERR_FILE;

    struct dirent *entry;
    char full_path[PRT_MAX_PATH * 2];
    char arch_path[PRT_MAX_PATH * 2];

    /* Determine base path for archive */
    const char *dir_name = strrchr(dir_path, '/');
    dir_name = dir_name ? dir_name + 1 : dir_path;

    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        int result = build_path(full_path, sizeof(full_path), dir_path, entry->d_name);
        if (result != PRT_OK) {
            fprintf(stderr, "Error: Path too long: %s/%s\n", dir_path, entry->d_name);
            closedir(dir);
            return result;
        }

        if (base_path) {
            result = build_path(arch_path, sizeof(arch_path), base_path, entry->d_name);
        } else {
            result = build_path(arch_path, sizeof(arch_path), dir_name, entry->d_name);
        }
        if (result != PRT_OK) {
            fprintf(stderr, "Error: Archive path too long: %s/%s\n",
                    base_path ? base_path : dir_name, entry->d_name);
            closedir(dir);
            return result;
        }

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            result = prt_archive_add_dir(archive, full_path, arch_path);
            if (result != PRT_OK) {
                closedir(dir);
                return result;
            }
        } else if (S_ISREG(st.st_mode)) {
            result = prt_archive_add_file(archive, full_path, arch_path);
            if (result != PRT_OK) {
                closedir(dir);
                return result;
            }
        }
    }

    closedir(dir);
    return PRT_OK;
}

int prt_archive_finalize(prt_archive_t *archive) {
    if (!archive || archive->mode != 1) return PRT_ERR_FILE;
    
    uint64_t table_offset = ftell(archive->file);
    
    for (uint32_t i = 0; i < archive->file_count; i++) {
        prt_file_entry_t *entry = &archive->entries[i];
        
        /* Write path length + path */
        uint16_t path_len = strlen(entry->path);
        fwrite(&path_len, 2, 1, archive->file);
        fwrite(entry->path, 1, path_len, archive->file);
        
        /* Write sizes, offset, and flags */
        uint8_t entry_data[17];
        write_u32_le(entry_data, entry->original_size);
        write_u32_le(entry_data + 4, entry->compressed_size);
        write_u64_le(entry_data + 8, entry->offset);
        entry_data[16] = entry->flags;
        fwrite(entry_data, 1, 17, archive->file);
    }
    
    fseek(archive->file, 0, SEEK_SET);
    
    uint8_t header[PRT_ARCHIVE_HEADER_SIZE];
    memcpy(header, PRT_ARCHIVE_MAGIC, 4);
    write_u32_le(header + 4, 2);  /* Version 2: has flags */
    write_u32_le(header + 8, archive->file_count);
    write_u64_le(header + 12, table_offset);
    
    fwrite(header, 1, PRT_ARCHIVE_HEADER_SIZE, archive->file);
    
    return PRT_OK;
}

uint32_t prt_archive_file_count(prt_archive_t *archive) {
    return archive ? archive->file_count : 0;
}

int prt_archive_get_entry(prt_archive_t *archive, uint32_t index,
                          prt_file_entry_t *entry) {
    if (!archive || index >= archive->file_count) return PRT_ERR_FORMAT;
    
    *entry = archive->entries[index];
    return PRT_OK;
}

int prt_archive_extract_file(prt_archive_t *archive, uint32_t index,
                             const char *output_path) {
    if (!archive || archive->mode != 0) return PRT_ERR_FILE;
    if (index >= archive->file_count) return PRT_ERR_FORMAT;
    
    prt_file_entry_t *entry = &archive->entries[index];
    
    fseek(archive->file, entry->offset, SEEK_SET);
    
    uint8_t *file_data = malloc(entry->compressed_size > 0 ? entry->compressed_size : 1);
    if (!file_data) return PRT_ERR_MEMORY;
    
    if (entry->compressed_size > 0 && 
        fread(file_data, 1, entry->compressed_size, archive->file) != entry->compressed_size) {
        free(file_data);
        return PRT_ERR_FILE;
    }
    
    uint8_t *output_data;
    size_t output_size;
    
    if (entry->flags & PRT_FLAG_STORED) {
        output_data = file_data;
        output_size = entry->original_size;
    } else {
        output_data = malloc(entry->original_size > 0 ? entry->original_size : 1);
        if (!output_data) {
            free(file_data);
            return PRT_ERR_MEMORY;
        }
        
        output_size = entry->original_size;
        int result = prt_decompress(file_data, entry->compressed_size, output_data, &output_size);
        free(file_data);
        
        if (result != PRT_OK) {
            free(output_data);
            return result;
        }
    }
    
    const char *final_path = output_path ? output_path : entry->path;
    
    char dir[PRT_MAX_PATH];
    get_dirname(final_path, dir);
    if (dir[0]) {
        mkdir_p(dir);
    }
    
    FILE *fout = fopen(final_path, "wb");
    if (!fout) {
        free(output_data);
        return PRT_ERR_FILE;
    }
    
    if (output_size > 0 && fwrite(output_data, 1, output_size, fout) != output_size) {
        free(output_data);
        fclose(fout);
        return PRT_ERR_FILE;
    }
    
    free(output_data);
    fclose(fout);
    return PRT_OK;
}

int prt_archive_extract_all(prt_archive_t *archive, const char *output_dir) {
    if (!archive || archive->mode != 0) return PRT_ERR_FILE;

    for (uint32_t i = 0; i < archive->file_count; i++) {
        char output_path[PRT_MAX_PATH * 2];
        int result;

        if (output_dir) {
            result = build_path(output_path, sizeof(output_path),
                               output_dir, archive->entries[i].path);
            if (result != PRT_OK) {
                fprintf(stderr, "Error: Output path too long: %s/%s\n",
                        output_dir, archive->entries[i].path);
                return result;
            }
        } else {
            strncpy(output_path, archive->entries[i].path, sizeof(output_path) - 1);
            output_path[sizeof(output_path) - 1] = '\0';
        }

        printf("  Extracting: %s\n", archive->entries[i].path);

        result = prt_archive_extract_file(archive, i, output_path);
        if (result != PRT_OK) {
            return result;
        }
    }

    return PRT_OK;
}

void prt_archive_list(prt_archive_t *archive) {
    if (!archive) return;
    
    printf("Archive: %s\n", archive->path);
    printf("Files: %u\n\n", archive->file_count);
    printf("%-40s %12s %12s %6s %s\n", "Path", "Original", "Compressed", "Ratio", "");
    printf("%-40s %12s %12s %6s %s\n", "----", "--------", "----------", "-----", "");
    
    uint64_t total_orig = 0, total_comp = 0;
    uint32_t stored_count = 0;
    
    for (uint32_t i = 0; i < archive->file_count; i++) {
        prt_file_entry_t *e = &archive->entries[i];
        double ratio = e->original_size > 0 ? 
                       (double)e->compressed_size / e->original_size * 100.0 : 0;
        
        const char *mode = (e->flags & PRT_FLAG_STORED) ? "[S]" : "";
        if (e->flags & PRT_FLAG_STORED) stored_count++;
        
        printf("%-40s %12u %12u %5.1f%% %s\n", 
               e->path, e->original_size, e->compressed_size, ratio, mode);
        
        total_orig += e->original_size;
        total_comp += e->compressed_size;
    }
    
    printf("%-40s %12s %12s %6s\n", "----", "--------", "----------", "-----");
    double total_ratio = total_orig > 0 ? (double)total_comp / total_orig * 100.0 : 0;
    printf("%-40s %12lu %12lu %5.1f%%\n", "TOTAL", total_orig, total_comp, total_ratio);
    printf("\n%u files stored, %u compressed\n", stored_count, archive->file_count - stored_count);
}
