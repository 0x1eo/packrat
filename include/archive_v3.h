/* packrat Archive v3 - Solid block compression */

#ifndef ARCHIVE_V3_H
#define ARCHIVE_V3_H

#include <stddef.h>
#include <stdint.h>

#define PRT_ARCHIVE_MAGIC_V3 "PRT\x03"

#define PRT_MAX_PATH_V3         512
#define PRT_SOLID_BLOCK_MAX     (1 * 1024 * 1024)
#define PRT_SOLID_BLOCK_MAX_FILES 1024

#define PRT_METHOD_STORED   0
#define PRT_METHOD_BWT      1

/* File groups for solid compression */
#define PRT_GROUP_WEB       0   /* .vue, .jsx, .tsx, .svelte */
#define PRT_GROUP_JS        1   /* .js, .ts, .mjs, .cjs */
#define PRT_GROUP_C         2   /* .c, .h, .cpp, .hpp */
#define PRT_GROUP_PYTHON    3   /* .py, .pyx, .pyi */
#define PRT_GROUP_CONFIG    4   /* .json, .yaml, .yml, .toml */
#define PRT_GROUP_DOCS      5   /* .md, .txt, .rst */
#define PRT_GROUP_STYLES    6   /* .css, .scss, .less */
#define PRT_GROUP_BINARY    7   /* Already compressed / binary */
#define PRT_GROUP_ASM       8   /* .s, .S, .asm */
#define PRT_GROUP_SHELL     9   /* .sh, .bash, .zsh, .fish */
#define PRT_GROUP_RUST     10   /* .rs */
#define PRT_GROUP_MAKE     11   /* Makefile, .mk */
#define PRT_GROUP_OTHER    12   /* Everything else */
#define PRT_GROUP_COUNT    13

typedef struct {
    uint8_t  magic[4];
    uint32_t version;
    uint32_t block_count;
    uint32_t file_count;
    uint64_t file_table_offset;
    uint64_t block_table_offset;
} prt_archive_header_v3_t;

#define PRT_ARCHIVE_HEADER_V3_SIZE 32

typedef struct {
    uint64_t offset;
    uint64_t compressed_size;
    uint64_t original_size;
    uint32_t file_start_idx;
    uint16_t file_count;
    uint8_t  method;
    uint8_t  group_id;
} prt_solid_block_entry_t;

#define PRT_SOLID_BLOCK_ENTRY_SIZE 32

typedef struct {
    uint64_t original_size;
    uint64_t offset_in_block;
    uint32_t block_idx;
    uint32_t crc32;
    uint16_t path_len;
    uint8_t  flags;
    uint8_t  reserved;
} prt_file_entry_v3_t;

#define PRT_FILE_ENTRY_V3_FIXED_SIZE 24

typedef struct {
    uint8_t *data;
    size_t data_size;
    size_t data_capacity;
    uint32_t *file_offsets;
    uint32_t *file_sizes;
    uint32_t file_count;
    uint32_t file_capacity;
    uint8_t group_id;
    uint8_t method;
} prt_block_builder_t;

typedef struct prt_solid_archive prt_solid_archive_t;

/* Creation */
prt_solid_archive_t* prt_solid_archive_create(const char *path);
int prt_solid_archive_add_file(prt_solid_archive_t *archive, const char *file_path, const char *archive_path);
int prt_solid_archive_add_dir(prt_solid_archive_t *archive, const char *dir_path, const char *base_path);
int prt_solid_archive_finalize(prt_solid_archive_t *archive);
void prt_solid_archive_close(prt_solid_archive_t *archive);

/* Reading */
prt_solid_archive_t* prt_solid_archive_open(const char *path);
uint32_t prt_solid_archive_file_count(prt_solid_archive_t *archive);
uint32_t prt_solid_archive_block_count(prt_solid_archive_t *archive);
int prt_solid_archive_extract_file(prt_solid_archive_t *archive, uint32_t file_idx, const char *output_path);
int prt_solid_archive_extract_all(prt_solid_archive_t *archive, const char *output_dir);
void prt_solid_archive_list(prt_solid_archive_t *archive);

/* Utility */
uint8_t prt_get_file_group(const char *path);
int prt_is_incompressible(const uint8_t *data, size_t size);
float prt_estimate_entropy(const uint8_t *data, size_t size);

#endif /* ARCHIVE_V3_H */
