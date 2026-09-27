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

/* <0: a before b by (category, title). */
static int cmp_app(const stark_app_t *a, const stark_app_t *b)
{
    int c = cmp_str(a->category, b->category);
    return c != 0 ? c : cmp_str(a->title, b->title);
}

size_t app_catalog_sort(const stark_app_t *const *apps, size_t n, const stark_app_t **out,
                        size_t max)
{
    if (apps == NULL || out == NULL) {
        return 0;
    }
    size_t count = n < max ? n : max;
    /* Insertion sort: stable, and n is a handful of apps. */
    for (size_t i = 0; i < count; i++) {
        const stark_app_t *app = apps[i];
        size_t j = i;
        while (j > 0 && cmp_app(out[j - 1], app) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = app;
    }
    return count;
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
