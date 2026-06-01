#ifndef EO_OPTIMIZER_H
#define EO_OPTIMIZER_H

#include "types.h"

typedef struct {
    int ytd_index;
    int tex_index;
} DupEntry;

typedef struct {
    char hash_key[97];   /* up to "name|sha256" */
    DupEntry *entries;
    int count;
} DupGroup;

typedef enum {
    DUP_BY_NAME = 0,
    DUP_BY_HASH = 1,
    DUP_BY_NAME_AND_HASH = 2,
} DupCriterion;

typedef enum {
    MIGRATE_KEEP_ORIGINAL = 0,   /* keep master in original YTD, drop dups; no consolidated */
    MIGRATE_MIXED         = 1,   /* keep master in original + copy to consolidated; drop dups */
    MIGRATE_REMOVE_DUPS   = 2,   /* move every distinct-name instance to consolidated; clear originals */
} MigrateStrategy;

/* Migration limit (16 MiB per consolidated YTD before splitting) */
#define MIGRATE_GREEN_LIMIT (16ULL * 1024ULL * 1024ULL)

/* Find duplicate textures across all loaded YTDs */
DupGroup *optimizer_find_duplicates(YtdFile **ytds, int ytd_count, int *out_group_count, DupCriterion criterion);

void optimizer_free_groups(DupGroup *groups, int count);

/* Migrate duplicates. New consolidated YtdFiles are appended to *io_ytds (up to max_ytds).
 * Returns the number of consolidated YTDs created (0 on error or strategy=KEEP_ORIGINAL).
 * Originals touched have ->modified set to true. Consolidated YtdFiles have ->modified = true. */
int optimizer_migrate_duplicates(YtdFile **io_ytds, int *io_ytd_count, int max_ytds,
                                 DupCriterion criterion, MigrateStrategy strategy,
                                 int *out_dup_groups, int *out_textures_moved,
                                 int *out_consolidated);

/* Smart optimize: resize textures above a threshold */
int optimizer_smart_resize(YtdFile *ytd, int max_width, int max_height, TexFormat target_fmt, int max_mips);

#endif
