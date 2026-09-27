/*
 * app_internal.h — private to stark_app: the registry accessor and the pure
 * catalog functions (app_catalog.c, host-tested).
 */
#pragma once

#include <stddef.h>
#include "stark_app.h"

/*
 * Stable sort of the first min(n, max) apps into out, returning that count:
 * categories in the order their first app appears in the registry, apps
 * within a category by title (NULL sorts as ""); equal keys keep registry
 * order (STARK-0107). The launcher passes max = n.
 */
size_t app_catalog_sort(const stark_app_t *const *apps, size_t n, const stark_app_t **out,
                        size_t max);

/* One launcher row: a category header (app NULL) or an app (header NULL). */
typedef struct {
    const char *header;
    const stark_app_t *app;
} app_catalog_row_t;

/*
 * The launcher's rows for apps already sorted by app_catalog_sort(): a
 * header row before the first app of each category (a NULL category's
 * header is ""), then the apps. Writes at most max rows and returns how many
 * were written; n apps need at most 2 * n.
 */
size_t app_catalog_layout(const stark_app_t *const *sorted, size_t n, app_catalog_row_t *out,
                          size_t max);

/* The app whose id equals `id`, or NULL (also for a NULL id). */
const stark_app_t *app_catalog_find(const stark_app_t *const *apps, size_t n, const char *id);
