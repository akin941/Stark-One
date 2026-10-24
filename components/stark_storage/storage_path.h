/*
 * storage_path.h — private to stark_storage: name rules and path building
 * (TASKS.md STARK-0203). Pure; host-tested at 100 % of lines.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "stark_err.h"
#include "stark_storage.h"

#define STORAGE_NAME_MAX 32
#define STORAGE_PATH_MAX 80 /* "<root>/stark/captures/<32>.tmp" with room to spare */

/* 1..32 of [A-Za-z0-9._-], not starting with '.', not ending in ".tmp". */
bool storage_name_valid(const char *name);

/* "<root>/stark/<dir>" (dir_only) or "<root>/stark/<dir>/<name>[.tmp]".
 * STARK_ERR_INVALID_ARG for a bad dir or name or a too-small buffer. */
stark_err_t storage_path_build(const char *root, stark_dir_t dir, const char *name, bool tmp,
                               char *out, size_t out_len);
stark_err_t storage_dir_path(const char *root, stark_dir_t dir, char *out, size_t out_len);

/* The four directory names, in stark_dir_t order. */
const char *storage_dir_name(stark_dir_t dir);

/* errno -> stark_err_t: ENOSPC NO_SPACE, ENOENT NOT_FOUND, EEXIST STATE,
 * EINVAL/ENAMETOOLONG INVALID_ARG, everything else IO. */
stark_err_t storage_map_errno(int e);
