/*
 * storage_path.c — name rules, path building, errno mapping
 * (storage_path.h). Pure.
 */
#include "storage_path.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *const k_dirs[] = {"captures", "logs", "apps", "config"};

const char *storage_dir_name(stark_dir_t dir)
{
    return (unsigned)dir < sizeof k_dirs / sizeof k_dirs[0] ? k_dirs[dir] : NULL;
}

bool storage_name_valid(const char *name)
{
    if (name == NULL || name[0] == '\0' || name[0] == '.') {
        return false;
    }
    size_t n = strlen(name);
    if (n > STORAGE_NAME_MAX || (n >= 4 && strcmp(name + n - 4, ".tmp") == 0)) {
        return false;
    }
    for (size_t i = 0; i < n; i++) {
        char c = name[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '.' || c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

stark_err_t storage_dir_path(const char *root, stark_dir_t dir, char *out, size_t out_len)
{
    const char *d = storage_dir_name(dir);
    if (root == NULL || d == NULL || out == NULL) {
        return STARK_ERR_INVALID_ARG;
    }
    int n = snprintf(out, out_len, "%s/stark/%s", root, d);
    return n >= 0 && (size_t)n < out_len ? STARK_OK : STARK_ERR_INVALID_ARG;
}

stark_err_t storage_path_build(const char *root, stark_dir_t dir, const char *name, bool tmp,
                               char *out, size_t out_len)
{
    if (!storage_name_valid(name)) {
        return STARK_ERR_INVALID_ARG;
    }
    STARK_CHECK_RET(storage_dir_path(root, dir, out, out_len));
    size_t used = strlen(out);
    int n = snprintf(out + used, out_len - used, "/%s%s", name, tmp ? ".tmp" : "");
    return n >= 0 && (size_t)n < out_len - used ? STARK_OK : STARK_ERR_INVALID_ARG;
}

stark_err_t storage_map_errno(int e)
{
    switch (e) {
        case ENOSPC:
            return STARK_ERR_NO_SPACE;
        case ENOENT:
            return STARK_ERR_NOT_FOUND;
        case EEXIST:
            return STARK_ERR_STATE;
        case EINVAL:
        case ENAMETOOLONG:
            return STARK_ERR_INVALID_ARG;
        default:
            return STARK_ERR_IO;
    }
}
