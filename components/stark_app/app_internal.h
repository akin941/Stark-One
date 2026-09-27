/*
 * app_internal.h — private to stark_app: the registry accessor and the pure
 * catalog functions (app_catalog.c, host-tested).
 */
#pragma once

#include <stddef.h>
#include "stark_app.h"

/*
 * Stable sort by category, then title (NULL sorts as ""), of the first
 * min(n, max) apps into out; returns that count. Apps with equal keys keep
 * their registry order. The launcher passes max = n.
 */
size_t app_catalog_sort(const stark_app_t *const *apps, size_t n, const stark_app_t **out,
                        size_t max);

/* The app whose id equals `id`, or NULL (also for a NULL id). */
const stark_app_t *app_catalog_find(const stark_app_t *const *apps, size_t n, const char *id);
