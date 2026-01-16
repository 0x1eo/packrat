/* packrat Archive - Multi-file archive support */

#ifndef ARCHIVE_H
#define ARCHIVE_H

#include <stddef.h>
#include <stdint.h>

#define PRT_ARCHIVE_MAGIC "PRTA"
#define PRT_ARCHIVE_MAGIC_SIZE 4
#define PRT_MAX_PATH 256

#define PRT_ERR_TOO_MANY_FILES -10
#define PRT_ERR_PATH_TOO_LONG  -11

#define PRT_FLAG_STORED 0x01

typedef struct {
    char     path[PRT_MAX_PATH];
    uint32_t original_size;
    uint32_t compressed_size;
    uint64_t offset;
    uint8_t  flags;
} prt_file_entry_t;

typedef struct {
    uint8_t  magic[4];
    uint32_t version;
    uint32_t file_count;
    uint64_t table_offset;
} prt_archive_header_t;

#define PRT_ARCHIVE_HEADER_SIZE 20

typedef struct prt_archive prt_archive_t;

/* Creation */
prt_archive_t* prt_archive_create(const char *path);
prt_archive_t* prt_archive_open(const char *path);
void prt_archive_close(prt_archive_t *archive);
int prt_archive_add_file(prt_archive_t *archive, const char *file_path, const char *archive_path);
int prt_archive_add_dir(prt_archive_t *archive, const char *dir_path, const char *base_path);
int prt_archive_finalize(prt_archive_t *archive);

/* Reading */
uint32_t prt_archive_file_count(prt_archive_t *archive);
int prt_archive_get_entry(prt_archive_t *archive, uint32_t index, prt_file_entry_t *entry);
int prt_archive_extract_file(prt_archive_t *archive, uint32_t index, const char *output_path);
int prt_archive_extract_all(prt_archive_t *archive, const char *output_dir);
void prt_archive_list(prt_archive_t *archive);

#endif /* ARCHIVE_H */
