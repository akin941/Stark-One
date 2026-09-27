/*
 * apptest.h — private to App Test: its worker and fault items
 * (app_apptest_worker.c).
 */
#pragma once

#include <stddef.h>

/* Runs worker/fault item `which`: 0 Worker demo, 1 Fail, 2 Fail from
 * worker, 3 Worker ignores stop. */
void apptest_run(size_t which);
