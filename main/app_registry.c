/*
 * app_registry.c — the one explicit, static app registry (ADR-0009), at the
 * composition root (ADR-0016). No linker sections, no section walking: an
 * app is listed here or it does not exist.
 */
#include "app_registry.h"

/* One line per app — see app_registry.h. Kept out of clang-format so that
 * adding an app stays a one-line change. */
/* clang-format off */
const stark_app_t *const stark_apps[] = {
    &app_about,
    &app_diagnostics,
    &app_inputtest,
    &app_displaytest,
    &app_buzzertest,
    &app_apptest,
    &app_hello,
};
/* clang-format on */

const size_t stark_apps_count = sizeof stark_apps / sizeof stark_apps[0];
