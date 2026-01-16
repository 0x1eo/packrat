/* Solid archive with parallel compression */

#include "archive_v3.h"
#include "packrat.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <dirent.h>
#include <sys/stat.h>
#include <ctype.h>
#include <pthread.h>

#ifndef PRT_NUM_THREADS
#define PRT_NUM_THREADS 8
#endif

static const struct {
    const char *ext;
    uint8_t group;
} extension_map[] = {
    /* Web UI */
    {".vue",    PRT_GROUP_WEB},
    {".jsx",    PRT_GROUP_WEB},
    {".tsx",    PRT_GROUP_WEB},
    {".svelte", PRT_GROUP_WEB},
    
    /* JavaScript/TypeScript */
    {".js",     PRT_GROUP_JS},
    {".ts",     PRT_GROUP_JS},
    {".mjs",    PRT_GROUP_JS},
    {".cjs",    PRT_GROUP_JS},
    
    /* C/C++ */
    {".c",      PRT_GROUP_C},
    {".h",      PRT_GROUP_C},
    {".cpp",    PRT_GROUP_C},
    {".hpp",    PRT_GROUP_C},
    {".cc",     PRT_GROUP_C},
    
    /* Python */
    {".py",     PRT_GROUP_PYTHON},
    {".pyx",    PRT_GROUP_PYTHON},
    {".pyi",    PRT_GROUP_PYTHON},
    
    /* Assembly */
    {".s",      PRT_GROUP_ASM},
    {".S",      PRT_GROUP_ASM},
    {".asm",    PRT_GROUP_ASM},
    
    /* Shell */
    {".sh",     PRT_GROUP_SHELL},
    {".bash",   PRT_GROUP_SHELL},
    {".zsh",    PRT_GROUP_SHELL},
    {".fish",   PRT_GROUP_SHELL},
    
    /* Rust */
    {".rs",     PRT_GROUP_RUST},
    
    /* Makefile */
    {".mk",     PRT_GROUP_MAKE},
    
    /* Config */
    {".json",   PRT_GROUP_CONFIG},
    {".yaml",   PRT_GROUP_CONFIG},
    {".yml",    PRT_GROUP_CONFIG},
    {".toml",   PRT_GROUP_CONFIG},
    {".xml",    PRT_GROUP_CONFIG},
    
    /* Documentation */
    {".md",     PRT_GROUP_DOCS},
    {".txt",    PRT_GROUP_DOCS},
    {".rst",    PRT_GROUP_DOCS},
    
    /* Styles */
    {".css",    PRT_GROUP_STYLES},
    {".scss",   PRT_GROUP_STYLES},
    {".less",   PRT_GROUP_STYLES},
    {".sass",   PRT_GROUP_STYLES},
    
    /* Binary/compressed - store only */
    {".jpg",    PRT_GROUP_BINARY},
    {".jpeg",   PRT_GROUP_BINARY},
    {".png",    PRT_GROUP_BINARY},
    {".gif",    PRT_GROUP_BINARY},
    {".webp",   PRT_GROUP_BINARY},
    {".pdf",    PRT_GROUP_BINARY},
    {".zip",    PRT_GROUP_BINARY},
    {".gz",     PRT_GROUP_BINARY},
    {".bz2",    PRT_GROUP_BINARY},
    {".xz",     PRT_GROUP_BINARY},
    {".7z",     PRT_GROUP_BINARY},
    {".mp3",    PRT_GROUP_BINARY},
    {".mp4",    PRT_GROUP_BINARY},
    {".ico",    PRT_GROUP_BINARY},
    
    {NULL, 0}
};

typedef struct {
    char *path;
    char *archive_path;
    size_t size;
    uint8_t group;
} pending_file_t;

typedef struct {
    uint8_t *input_data;
    size_t input_size;
    uint8_t *output_data;
    size_t output_size;
    uint8_t method;
    uint8_t group_id;
    uint32_t file_start;
    uint32_t file_count;
    int result;
    volatile int done;
} block_work_t;

struct prt_solid_archive {
    FILE *file;
    char *path;
    int mode;
    
    pending_file_t *pending;
    uint32_t pending_count;
    uint32_t pending_capacity;
    
    prt_archive_header_v3_t header;
    prt_solid_block_entry_t *blocks;
    prt_file_entry_v3_t *files;
    char **file_paths;
    
    uint32_t cached_block_idx;
    uint8_t *cached_block_data;
    size_t cached_block_size;
};

static void write_u16_le(uint8_t *buf, uint16_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
}

static void write_u32_le(uint8_t *buf, uint32_t val) {
    buf[0] = val & 0xFF;
    buf[1] = (val >> 8) & 0xFF;
    buf[2] = (val >> 16) & 0xFF;
    buf[3] = (val >> 24) & 0xFF;
}

static void write_u64_le(uint8_t *buf, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        buf[i] = (val >> (i * 8)) & 0xFF;
    }
}

static uint16_t read_u16_le(const uint8_t *buf) {
    return buf[0] | ((uint16_t)buf[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *buf) {
    return buf[0] | (buf[1] << 8) | (buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

static uint64_t read_u64_le(const uint8_t *buf) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val |= ((uint64_t)buf[i]) << (i * 8);
    }
    return val;
}

static int mkdir_p(const char *path) {
    char tmp[PRT_MAX_PATH_V3];
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

static void get_dirname(const char *path, char *dir, size_t dir_size) {
    const char *last_slash = strrchr(path, '/');
    if (last_slash && (size_t)(last_slash - path) < dir_size) {
        size_t len = last_slash - path;
        memcpy(dir, path, len);
        dir[len] = '\0';
    } else {
        dir[0] = '\0';
    }
}

/* Get file extension (lowercase) */
static const char* get_extension(const char *path) {
    const char *dot = strrchr(path, '.');
    const char *slash = strrchr(path, '/');
    if (dot && (!slash || dot > slash)) {
        return dot;
    }
    return NULL;
}

static uint32_t crc32_table[256];
static int crc32_table_init = 0;

static void init_crc32_table(void) {
    if (crc32_table_init) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        }
        crc32_table[i] = c;
    }
    crc32_table_init = 1;
}

static uint32_t compute_crc32(const uint8_t *data, size_t len) {
    init_crc32_table();
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

uint8_t prt_get_file_group(const char *path) {
    const char *ext = get_extension(path);
    if (!ext) return PRT_GROUP_OTHER;
    
    char ext_lower[16];
    size_t i;
    for (i = 0; ext[i] && i < sizeof(ext_lower) - 1; i++) {
        ext_lower[i] = tolower((unsigned char)ext[i]);
    }
    ext_lower[i] = '\0';
    
    for (int j = 0; extension_map[j].ext; j++) {
        if (strcmp(ext_lower, extension_map[j].ext) == 0) {
            return extension_map[j].group;
        }
    }
    
    return PRT_GROUP_OTHER;
}

float prt_estimate_entropy(const uint8_t *data, size_t size) {
    if (size < 256) return 0.0f;
    
    uint32_t freq[256] = {0};
    
    size_t sample_size = (size < 4096) ? size : 4096;
    size_t positions[] = {0, size/2 - sample_size/2, size - sample_size};
    size_t total_samples = 0;
    
    for (int p = 0; p < 3 && positions[p] + sample_size <= size; p++) {
        for (size_t i = 0; i < sample_size; i++) {
            freq[data[positions[p] + i]]++;
        }
        total_samples += sample_size;
    }
    
    if (total_samples == 0) return 0.0f;
    
    float entropy = 0.0f;
    float total = (float)total_samples;
    for (int i = 0; i < 256; i++) {
        if (freq[i] > 0) {
            float prob = freq[i] / total;
            entropy -= prob * log2f(prob);
        }
    }
    
    return entropy;
}

int prt_is_incompressible(const uint8_t *data, size_t size) {
    if (size >= 4) {
        if (memcmp(data, "\x89PNG", 4) == 0) return 1;
        if (memcmp(data, "\xFF\xD8\xFF", 3) == 0) return 1;
        if (memcmp(data, "PK\x03\x04", 4) == 0) return 1;
        if (memcmp(data, "%PDF", 4) == 0) return 1;
        if (memcmp(data, "\x1F\x8B", 2) == 0) return 1;
        if (memcmp(data, "GIF8", 4) == 0) return 1;
        if (memcmp(data, "RIFF", 4) == 0) return 1;
        if (memcmp(data, "\x00\x00\x00", 3) == 0 && 
            (data[3] == 0x18 || data[3] == 0x1C || data[3] == 0x20)) return 1;
    }
    
    float entropy = prt_estimate_entropy(data, size);
    return entropy >= 7.5f;
}

prt_solid_archive_t* prt_solid_archive_create(const char *path) {
    prt_solid_archive_t *archive = calloc(1, sizeof(prt_solid_archive_t));
    if (!archive) return NULL;
    
    archive->file = fopen(path, "wb");
    if (!archive->file) {
        free(archive);
        return NULL;
    }
    
    archive->path = strdup(path);
    archive->mode = 1;
    archive->pending_capacity = 256;
    archive->pending = calloc(archive->pending_capacity, sizeof(pending_file_t));
    archive->cached_block_idx = UINT32_MAX;
    
    if (!archive->pending) {
        fclose(archive->file);
        free(archive->path);
        free(archive);
        return NULL;
    }
    
    uint8_t header[PRT_ARCHIVE_HEADER_V3_SIZE] = {0};
    memcpy(header, PRT_ARCHIVE_MAGIC_V3, 4);
    fwrite(header, 1, PRT_ARCHIVE_HEADER_V3_SIZE, archive->file);
    
    return archive;
}

int prt_solid_archive_add_file(prt_solid_archive_t *archive,
                                const char *file_path,
                                const char *archive_path) {
    if (!archive || archive->mode != 1) return PRT_ERR_FILE;
    
    struct stat st;
    if (stat(file_path, &st) != 0) return PRT_ERR_FILE;
    
    if (archive->pending_count >= archive->pending_capacity) {
        archive->pending_capacity *= 2;
        archive->pending = realloc(archive->pending,
            archive->pending_capacity * sizeof(pending_file_t));
        if (!archive->pending) return PRT_ERR_MEMORY;
    }
    
    const char *store_path = archive_path;
    if (!store_path) {
        store_path = strrchr(file_path, '/');
        store_path = store_path ? store_path + 1 : file_path;
    }
    
    pending_file_t *pf = &archive->pending[archive->pending_count++];
    pf->path = strdup(file_path);
    pf->archive_path = strdup(store_path);
    pf->size = st.st_size;
    pf->group = prt_get_file_group(file_path);
    
    return PRT_OK;
}

int prt_solid_archive_add_dir(prt_solid_archive_t *archive,
                               const char *dir_path,
                               const char *base_path) {
    DIR *dir = opendir(dir_path);
    if (!dir) return PRT_ERR_FILE;
    
    struct dirent *entry;
    char full_path[PRT_MAX_PATH_V3];
    char arch_path[PRT_MAX_PATH_V3];
    
    const char *dir_name = strrchr(dir_path, '/');
    dir_name = dir_name ? dir_name + 1 : dir_path;
    
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        
        if (base_path) {
            snprintf(arch_path, sizeof(arch_path), "%s/%s", base_path, entry->d_name);
        } else {
            snprintf(arch_path, sizeof(arch_path), "%s/%s", dir_name, entry->d_name);
        }
        
        struct stat st;
        if (stat(full_path, &st) != 0) continue;
        
        if (S_ISDIR(st.st_mode)) {
            int result = prt_solid_archive_add_dir(archive, full_path, arch_path);
            if (result != PRT_OK) {
                closedir(dir);
                return result;
            }
        } else if (S_ISREG(st.st_mode)) {
            int result = prt_solid_archive_add_file(archive, full_path, arch_path);
            if (result != PRT_OK) {
                closedir(dir);
                return result;
            }
        }
    }
    
    closedir(dir);
    return PRT_OK;
}

static int compare_pending(const void *a, const void *b) {
    const pending_file_t *pa = a;
    const pending_file_t *pb = b;
    if (pa->group != pb->group) return pa->group - pb->group;
    return strcmp(pa->archive_path, pb->archive_path);
}

static int compress_solid_block(const uint8_t *data, size_t size,
                                 uint8_t method,
                                 uint8_t **out_data, size_t *out_size) {
    if (method == PRT_METHOD_STORED || size == 0) {
        *out_data = malloc(size > 0 ? size : 1);
        if (!*out_data && size > 0) return PRT_ERR_MEMORY;
        if (size > 0) memcpy(*out_data, data, size);
        *out_size = size;
        return PRT_OK;
    }
    
    size_t max_size = size + PRT_HEADER_SIZE + 1024;
    uint8_t *compressed = malloc(max_size);
    if (!compressed) return PRT_ERR_MEMORY;
    
    size_t compressed_size;
    int result = prt_compress(data, size, compressed, &compressed_size);
    
    if (result != PRT_OK || compressed_size >= size) {
        free(compressed);
        *out_data = malloc(size);
        if (!*out_data) return PRT_ERR_MEMORY;
        memcpy(*out_data, data, size);
        *out_size = size;
        return PRT_OK;
    }
    
    *out_data = compressed;
    *out_size = compressed_size;
    return PRT_OK;
}

typedef struct {
    block_work_t *work_items;
    uint32_t block_count;
    volatile uint32_t *next_block;
    pthread_mutex_t *mutex;
} thread_arg_t;

static void* compression_worker(void *arg) {
    thread_arg_t *targ = (thread_arg_t*)arg;
    
    while (1) {
        pthread_mutex_lock(targ->mutex);
        uint32_t idx = (*targ->next_block)++;
        pthread_mutex_unlock(targ->mutex);
        
        if (idx >= targ->block_count) {
            break;
        }
        
        block_work_t *work = &targ->work_items[idx];
        
        work->result = compress_solid_block(
            work->input_data, work->input_size,
            work->method,
            &work->output_data, &work->output_size
        );
        
        work->done = 1;
    }
    
    return NULL;
}

int prt_solid_archive_finalize(prt_solid_archive_t *archive) {
    if (!archive || archive->mode != 1) return PRT_ERR_FILE;
    
    qsort(archive->pending, archive->pending_count, 
          sizeof(pending_file_t), compare_pending);
    
    uint32_t max_blocks = archive->pending_count + 1;
    block_work_t *work_items = calloc(max_blocks, sizeof(block_work_t));
    prt_file_entry_v3_t *files = calloc(archive->pending_count, sizeof(prt_file_entry_v3_t));
    
    if (!work_items || !files) {
        free(work_items);
        free(files);
        return PRT_ERR_MEMORY;
    }
    
    uint32_t block_count = 0;
    uint32_t i = 0;
    
    printf("  Preparing blocks...\n");
    
    while (i < archive->pending_count) {
        pending_file_t *pf = &archive->pending[i];
        uint8_t current_group = pf->group;
        uint8_t method = (current_group == PRT_GROUP_BINARY) ? 
                         PRT_METHOD_STORED : PRT_METHOD_BWT;
        
        size_t block_data_size = 0;
        uint32_t block_file_start = i;
        uint32_t block_file_count = 0;
        
        do {
            block_data_size += archive->pending[i].size;
            block_file_count++;
            i++;
        } while (i < archive->pending_count &&
                 archive->pending[i].group == current_group &&
                 block_data_size + archive->pending[i].size <= PRT_SOLID_BLOCK_MAX &&
                 block_file_count < PRT_SOLID_BLOCK_MAX_FILES);
        
        uint8_t *block_data = malloc(block_data_size > 0 ? block_data_size : 1);
        if (!block_data && block_data_size > 0) {
            for (uint32_t j = 0; j < block_count; j++) {
                free(work_items[j].input_data);
            }
            free(work_items);
            free(files);
            return PRT_ERR_MEMORY;
        }
        
        size_t offset_in_block = 0;
        for (uint32_t j = block_file_start; j < block_file_start + block_file_count; j++) {
            pending_file_t *file = &archive->pending[j];
            
            FILE *fin = fopen(file->path, "rb");
            if (fin && file->size > 0) {
                fread(block_data + offset_in_block, 1, file->size, fin);
                fclose(fin);
            }
            
            files[j].original_size = file->size;
            files[j].offset_in_block = offset_in_block;
            files[j].block_idx = block_count;
            files[j].crc32 = (file->size > 0) ? 
                             compute_crc32(block_data + offset_in_block, file->size) : 0;
            files[j].path_len = strlen(file->archive_path);
            files[j].flags = 0;
            
            offset_in_block += file->size;
        }
        
        if (method == PRT_METHOD_BWT && block_data_size > 0 &&
            prt_is_incompressible(block_data, block_data_size)) {
            method = PRT_METHOD_STORED;
        }
        
        work_items[block_count].input_data = block_data;
        work_items[block_count].input_size = block_data_size;
        work_items[block_count].method = method;
        work_items[block_count].group_id = current_group;
        work_items[block_count].file_start = block_file_start;
        work_items[block_count].file_count = block_file_count;
        work_items[block_count].done = 0;
        
        block_count++;
    }
    
    int num_threads = PRT_NUM_THREADS;
    if ((uint32_t)num_threads > block_count) {
        num_threads = block_count;
    }
    
    printf("  Compressing %u blocks using %d threads...\n", block_count, num_threads);
    
    pthread_mutex_t work_mutex = PTHREAD_MUTEX_INITIALIZER;
    volatile uint32_t next_block = 0;
    
    thread_arg_t targ;
    targ.work_items = work_items;
    targ.block_count = block_count;
    targ.next_block = &next_block;
    targ.mutex = &work_mutex;
    
    pthread_t threads[PRT_NUM_THREADS];
    for (int t = 0; t < num_threads; t++) {
        pthread_create(&threads[t], NULL, compression_worker, &targ);
    }
    
    uint32_t last_done = 0;
    while (1) {
        uint32_t done_count = 0;
        for (uint32_t b = 0; b < block_count; b++) {
            if (work_items[b].done) done_count++;
        }
        
        if (done_count != last_done) {
            printf("\r  Progress: %u/%u blocks completed", done_count, block_count);
            fflush(stdout);
            last_done = done_count;
        }
        
        if (done_count >= block_count) break;
        
        struct timespec ts = {0, 100000000};
        nanosleep(&ts, NULL);
    }
    printf("\n");
    
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
    }
    pthread_mutex_destroy(&work_mutex);
    
    for (uint32_t b = 0; b < block_count; b++) {
        block_work_t *work = &work_items[b];
        
        if (work->result != PRT_OK) {
            for (uint32_t j = 0; j < block_count; j++) {
                free(work_items[j].input_data);
                free(work_items[j].output_data);
            }
            free(work_items);
            free(files);
            return work->result;
        }
        
        double ratio = work->input_size > 0 ? 
                       100.0 * work->output_size / work->input_size : 0;
        printf("  Block %u: %u files, %lu -> %lu (%.1f%%)\n",
               b + 1, work->file_count,
               (unsigned long)work->input_size,
               (unsigned long)work->output_size, ratio);
    }
    
    prt_solid_block_entry_t *blocks = calloc(block_count, sizeof(prt_solid_block_entry_t));
    if (!blocks) {
        for (uint32_t j = 0; j < block_count; j++) {
            free(work_items[j].input_data);
            free(work_items[j].output_data);
        }
        free(work_items);
        free(files);
        return PRT_ERR_MEMORY;
    }
    
    uint64_t data_offset = PRT_ARCHIVE_HEADER_V3_SIZE;
    
    for (uint32_t b = 0; b < block_count; b++) {
        block_work_t *work = &work_items[b];
        
        blocks[b].offset = data_offset;
        blocks[b].compressed_size = work->output_size;
        blocks[b].original_size = work->input_size;
        blocks[b].file_start_idx = work->file_start;
        blocks[b].file_count = work->file_count;
        blocks[b].method = (work->output_size < work->input_size) ? work->method : PRT_METHOD_STORED;
        blocks[b].group_id = work->group_id;
        
        if (work->output_size > 0) {
            fwrite(work->output_data, 1, work->output_size, archive->file);
        }
        
        data_offset += work->output_size;
        
        free(work->input_data);
        free(work->output_data);
    }
    
    free(work_items);
    
    uint64_t block_table_offset = ftell(archive->file);
    for (uint32_t b = 0; b < block_count; b++) {
        uint8_t buf[PRT_SOLID_BLOCK_ENTRY_SIZE];
        write_u64_le(buf, blocks[b].offset);
        write_u64_le(buf + 8, blocks[b].compressed_size);
        write_u64_le(buf + 16, blocks[b].original_size);
        write_u32_le(buf + 24, blocks[b].file_start_idx);
        write_u16_le(buf + 28, blocks[b].file_count);
        buf[30] = blocks[b].method;
        buf[31] = blocks[b].group_id;
        fwrite(buf, 1, PRT_SOLID_BLOCK_ENTRY_SIZE, archive->file);
    }
    
    uint64_t file_table_offset = ftell(archive->file);
    for (uint32_t f = 0; f < archive->pending_count; f++) {
        uint8_t buf[PRT_FILE_ENTRY_V3_FIXED_SIZE];
        write_u64_le(buf, files[f].original_size);
        write_u64_le(buf + 8, files[f].offset_in_block);
        write_u32_le(buf + 16, files[f].block_idx);
        write_u32_le(buf + 20, files[f].crc32);
        fwrite(buf, 1, PRT_FILE_ENTRY_V3_FIXED_SIZE, archive->file);
        
        uint16_t path_len = strlen(archive->pending[f].archive_path);
        fwrite(&path_len, 2, 1, archive->file);
        fwrite(archive->pending[f].archive_path, 1, path_len, archive->file);
    }
    
    fseek(archive->file, 0, SEEK_SET);
    uint8_t header[PRT_ARCHIVE_HEADER_V3_SIZE];
    memcpy(header, PRT_ARCHIVE_MAGIC_V3, 4);
    write_u32_le(header + 4, 3);
    write_u32_le(header + 8, block_count);
    write_u32_le(header + 12, archive->pending_count);
    write_u64_le(header + 16, file_table_offset);
    write_u64_le(header + 24, block_table_offset);
    fwrite(header, 1, PRT_ARCHIVE_HEADER_V3_SIZE, archive->file);
    
    free(blocks);
    free(files);
    
    return PRT_OK;
}

void prt_solid_archive_close(prt_solid_archive_t *archive) {
    if (!archive) return;
    
    if (archive->file) fclose(archive->file);
    free(archive->path);
    
    for (uint32_t i = 0; i < archive->pending_count; i++) {
        free(archive->pending[i].path);
        free(archive->pending[i].archive_path);
    }
    free(archive->pending);
    
    free(archive->blocks);
    if (archive->files) {
        for (uint32_t i = 0; i < archive->header.file_count; i++) {
            free(archive->file_paths[i]);
        }
        free(archive->file_paths);
    }
    free(archive->files);
    free(archive->cached_block_data);
    
    free(archive);
}

prt_solid_archive_t* prt_solid_archive_open(const char *path) {
    prt_solid_archive_t *archive = calloc(1, sizeof(prt_solid_archive_t));
    if (!archive) return NULL;
    
    archive->file = fopen(path, "rb");
    if (!archive->file) {
        free(archive);
        return NULL;
    }
    
    archive->path = strdup(path);
    archive->mode = 0;
    archive->cached_block_idx = UINT32_MAX;
    
    uint8_t header_buf[PRT_ARCHIVE_HEADER_V3_SIZE];
    if (fread(header_buf, 1, PRT_ARCHIVE_HEADER_V3_SIZE, archive->file) != PRT_ARCHIVE_HEADER_V3_SIZE) {
        goto error;
    }
    
    if (memcmp(header_buf, PRT_ARCHIVE_MAGIC_V3, 4) != 0) {
        goto error;
    }
    
    archive->header.version = read_u32_le(header_buf + 4);
    archive->header.block_count = read_u32_le(header_buf + 8);
    archive->header.file_count = read_u32_le(header_buf + 12);
    archive->header.file_table_offset = read_u64_le(header_buf + 16);
    archive->header.block_table_offset = read_u64_le(header_buf + 24);
    
    archive->blocks = calloc(archive->header.block_count, sizeof(prt_solid_block_entry_t));
    if (!archive->blocks && archive->header.block_count > 0) goto error;
    
    fseek(archive->file, archive->header.block_table_offset, SEEK_SET);
    for (uint32_t i = 0; i < archive->header.block_count; i++) {
        uint8_t buf[PRT_SOLID_BLOCK_ENTRY_SIZE];
        if (fread(buf, 1, PRT_SOLID_BLOCK_ENTRY_SIZE, archive->file) != PRT_SOLID_BLOCK_ENTRY_SIZE) {
            goto error;
        }
        archive->blocks[i].offset = read_u64_le(buf);
        archive->blocks[i].compressed_size = read_u64_le(buf + 8);
        archive->blocks[i].original_size = read_u64_le(buf + 16);
        archive->blocks[i].file_start_idx = read_u32_le(buf + 24);
        archive->blocks[i].file_count = read_u16_le(buf + 28);
        archive->blocks[i].method = buf[30];
        archive->blocks[i].group_id = buf[31];
    }
    
    archive->files = calloc(archive->header.file_count, sizeof(prt_file_entry_v3_t));
    archive->file_paths = calloc(archive->header.file_count, sizeof(char*));
    if ((!archive->files || !archive->file_paths) && archive->header.file_count > 0) goto error;
    
    fseek(archive->file, archive->header.file_table_offset, SEEK_SET);
    for (uint32_t i = 0; i < archive->header.file_count; i++) {
        uint8_t buf[PRT_FILE_ENTRY_V3_FIXED_SIZE];
        if (fread(buf, 1, PRT_FILE_ENTRY_V3_FIXED_SIZE, archive->file) != PRT_FILE_ENTRY_V3_FIXED_SIZE) {
            goto error;
        }
        archive->files[i].original_size = read_u64_le(buf);
        archive->files[i].offset_in_block = read_u64_le(buf + 8);
        archive->files[i].block_idx = read_u32_le(buf + 16);
        archive->files[i].crc32 = read_u32_le(buf + 20);
        
        uint16_t path_len;
        if (fread(&path_len, 2, 1, archive->file) != 1) goto error;
        
        archive->file_paths[i] = malloc(path_len + 1);
        if (!archive->file_paths[i]) goto error;
        if (fread(archive->file_paths[i], 1, path_len, archive->file) != path_len) goto error;
        archive->file_paths[i][path_len] = '\0';
    }
    
    return archive;
    
error:
    prt_solid_archive_close(archive);
    return NULL;
}

uint32_t prt_solid_archive_file_count(prt_solid_archive_t *archive) {
    return archive ? archive->header.file_count : 0;
}

uint32_t prt_solid_archive_block_count(prt_solid_archive_t *archive) {
    return archive ? archive->header.block_count : 0;
}

static int decompress_block(prt_solid_archive_t *archive, uint32_t block_idx) {
    if (block_idx == archive->cached_block_idx) {
        return PRT_OK;
    }
    
    prt_solid_block_entry_t *block = &archive->blocks[block_idx];
    
    fseek(archive->file, block->offset, SEEK_SET);
    uint8_t *compressed = malloc(block->compressed_size > 0 ? block->compressed_size : 1);
    if (!compressed && block->compressed_size > 0) return PRT_ERR_MEMORY;
    
    if (block->compressed_size > 0 &&
        fread(compressed, 1, block->compressed_size, archive->file) != block->compressed_size) {
        free(compressed);
        return PRT_ERR_FILE;
    }
    
    free(archive->cached_block_data);
    
    if (block->method == PRT_METHOD_STORED) {
        archive->cached_block_data = compressed;
        archive->cached_block_size = block->original_size;
    } else {
        archive->cached_block_data = malloc(block->original_size > 0 ? block->original_size : 1);
        if (!archive->cached_block_data && block->original_size > 0) {
            free(compressed);
            return PRT_ERR_MEMORY;
        }
        
        size_t out_size = block->original_size;
        int result = prt_decompress(compressed, block->compressed_size,
                                    archive->cached_block_data, &out_size);
        free(compressed);
        
        if (result != PRT_OK) {
            free(archive->cached_block_data);
            archive->cached_block_data = NULL;
            return result;
        }
        
        archive->cached_block_size = out_size;
    }
    
    archive->cached_block_idx = block_idx;
    return PRT_OK;
}

int prt_solid_archive_extract_file(prt_solid_archive_t *archive,
                                    uint32_t file_idx,
                                    const char *output_path) {
    if (!archive || archive->mode != 0) return PRT_ERR_FILE;
    if (file_idx >= archive->header.file_count) return PRT_ERR_FORMAT;
    
    prt_file_entry_v3_t *file = &archive->files[file_idx];
    
    int result = decompress_block(archive, file->block_idx);
    if (result != PRT_OK) return result;
    
    const char *final_path = output_path ? output_path : archive->file_paths[file_idx];
    
    char dir[PRT_MAX_PATH_V3];
    get_dirname(final_path, dir, sizeof(dir));
    if (dir[0]) mkdir_p(dir);
    
    FILE *fout = fopen(final_path, "wb");
    if (!fout) return PRT_ERR_FILE;
    
    if (file->original_size > 0) {
        const uint8_t *data = archive->cached_block_data + file->offset_in_block;
        if (fwrite(data, 1, file->original_size, fout) != file->original_size) {
            fclose(fout);
            return PRT_ERR_FILE;
        }
    }
    
    fclose(fout);
    return PRT_OK;
}

int prt_solid_archive_extract_all(prt_solid_archive_t *archive, const char *output_dir) {
    if (!archive || archive->mode != 0) return PRT_ERR_FILE;
    
    for (uint32_t i = 0; i < archive->header.file_count; i++) {
        char output_path[PRT_MAX_PATH_V3 * 2];
        
        if (output_dir) {
            snprintf(output_path, sizeof(output_path), "%s/%s",
                     output_dir, archive->file_paths[i]);
        } else {
            strncpy(output_path, archive->file_paths[i], sizeof(output_path) - 1);
        }
        
        printf("  Extracting: %s\n", archive->file_paths[i]);
        
        int result = prt_solid_archive_extract_file(archive, i, output_path);
        if (result != PRT_OK) return result;
    }
    
    return PRT_OK;
}

static const char* group_name(uint8_t group) {
    static const char* names[] = {
        "Web", "JS", "C/C++", "Python", "Config", "Docs", "Styles", "Binary", "Other"
    };
    return group < PRT_GROUP_COUNT ? names[group] : "?";
}

void prt_solid_archive_list(prt_solid_archive_t *archive) {
    if (!archive) return;
    
    printf("Archive: %s (Solid v3)\n", archive->path);
    printf("Blocks: %u, Files: %u\n\n", 
           archive->header.block_count, archive->header.file_count);
    
    printf("Solid Blocks:\n");
    printf("  %-6s %-8s %12s %12s %6s\n", "Block", "Group", "Original", "Compressed", "Ratio");
    printf("  %-6s %-8s %12s %12s %6s\n", "-----", "-----", "--------", "----------", "-----");
    
    uint64_t total_orig = 0, total_comp = 0;
    for (uint32_t b = 0; b < archive->header.block_count; b++) {
        prt_solid_block_entry_t *block = &archive->blocks[b];
        double ratio = block->original_size > 0 ?
                       100.0 * block->compressed_size / block->original_size : 0;
        printf("  %-6u %-8s %12lu %12lu %5.1f%%\n",
               b, group_name(block->group_id),
               (unsigned long)block->original_size,
               (unsigned long)block->compressed_size, ratio);
        total_orig += block->original_size;
        total_comp += block->compressed_size;
    }
    printf("  %-6s %-8s %12s %12s %6s\n", "-----", "-----", "--------", "----------", "-----");
    printf("  %-6s %-8s %12lu %12lu %5.1f%%\n\n", "TOTAL", "",
           (unsigned long)total_orig, (unsigned long)total_comp,
           total_orig > 0 ? 100.0 * total_comp / total_orig : 0);
    
    if (archive->header.file_count > 100) {
        printf("Files: %u (use -v for full list)\n\n", archive->header.file_count);
    } else {
        printf("Files:\n");
        printf("  %-50s %12s %6s\n", "Path", "Size", "Block");
        printf("  %-50s %12s %6s\n", "----", "----", "-----");
        
        for (uint32_t i = 0; i < archive->header.file_count; i++) {
            printf("  %-50s %12lu %6u\n",
                   archive->file_paths[i],
                   (unsigned long)archive->files[i].original_size,
                   archive->files[i].block_idx);
        }
        printf("\n");
    }
    
    printf("%-40s %12s %12s %6s\n", "----", "--------", "----------", "-----");
    printf("%-40s %12lu %12lu %5.1f%%\n", "TOTAL",
           (unsigned long)total_orig, (unsigned long)total_comp,
           total_orig > 0 ? 100.0 * total_comp / total_orig : 0);
}
