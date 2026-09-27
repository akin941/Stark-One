/*
 * app_catalog.c — pure registry helpers (TASKS.md STARK-0019): launcher
 * order and lookup. Host-tested; no ESP-IDF.
 */
#include <string.h>
#include "app_internal.h"

static int cmp_str(const char *a, const char *b)
{
    return strcmp(a != NULL ? a : "", b != NULL ? b : "");
}

/* A category's rank: the registry index of the first app that has it
 * (STARK-0107 — the registry, not the alphabet, orders the categories). */
static size_t rank(const stark_app_t *const *apps, size_t n, const char *category)
{
    for (size_t i = 0; i < n; i++) {
        if (cmp_str(apps[i]->category, category) == 0) {
            return i;
        }
    }
    return n;
}

size_t app_catalog_sort(const stark_app_t *const *apps, size_t n, const stark_app_t **out,
                        size_t max)
{
    if (apps == NULL || out == NULL) {
        return 0;
    }
    size_t count = n < max ? n : max;
    /* Insertion sort by (category rank, title): stable, and n is a handful. */
    for (size_t i = 0; i < count; i++) {
        const stark_app_t *app = apps[i];
        size_t r = rank(apps, n, app->category);
        size_t j = i;
        while (j > 0) {
            size_t rp = rank(apps, n, out[j - 1]->category);
            if (rp < r || (rp == r && cmp_str(out[j - 1]->title, app->title) <= 0)) {
                break;
            }
            out[j] = out[j - 1];
            j--;
        }
        out[j] = app;
    }
    return count;
}

size_t app_catalog_layout(const stark_app_t *const *sorted, size_t n, app_catalog_row_t *out,
                          size_t max)
{
    if (sorted == NULL || out == NULL) {
        return 0;
    }
    size_t rows = 0;
    for (size_t i = 0; i < n && rows < max; i++) {
        const char *cat = sorted[i]->category != NULL ? sorted[i]->category : "";
        if (i == 0 || cmp_str(sorted[i - 1]->category, cat) != 0) {
            out[rows++] = (app_catalog_row_t){.header = cat, .app = NULL};
            if (rows == max) {
                break;
            }
        }
        out[rows++] = (app_catalog_row_t){.header = NULL, .app = sorted[i]};
    }
    return rows;
}

const stark_app_t *app_catalog_find(const stark_app_t *const *apps, size_t n, const char *id)
{
    if (apps == NULL || id == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < n; i++) {
        if (apps[i]->id != NULL && strcmp(apps[i]->id, id) == 0) {
            return apps[i];
        }
    }
    return NULL;
}
