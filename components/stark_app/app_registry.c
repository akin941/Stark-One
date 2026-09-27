/*
 * app_registry.c — the one explicit, static app registry (ADR-0009).
 * No linker sections, no section walking: an app is listed here or it does
 * not exist.
 */
#include "app_internal.h"
#include "app_list.h"

static const stark_app_t *const stark_apps[] = {
    /* one line per app, e.g. &app_about, — see app_list.h */
};

const stark_app_t *const *app_registry(size_t *count)
{
    *count = sizeof stark_apps / sizeof stark_apps[0];
    return stark_apps;
}
