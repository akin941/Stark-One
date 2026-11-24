/*
 * selftest_export.h — the self-test report written to the card (TASKS.md STARK-0707, ADR-0023
 * §8), pure over selftest_env_t. The report text is stark_diag's selftest_report.c; this file
 * gathers what goes into it, finds the next free `report_NNNN.txt`, streams the text into an
 * atomic card file (so a card that fails part-way leaves no partial file), and reads the file back
 * to compare its length and CRC-32 with what was written before calling it good. The report stays
 * on the device (SECURITY_SCOPE §2): nothing here sends anything anywhere.
 */
#pragma once

#include <stddef.h>
#include "selftest_env.h"

/*
 * Writes logs/report_NNNN.txt, NNNN being one more than the highest present (so the first is
 * report_0001.txt); copies the name into `name` (at least SELFTEST_REPORT_NAME_MAX bytes) and the
 * size into *bytes. STARK_ERR_STATE without a mounted card, STARK_ERR_NO_SPACE when 9 999 reports
 * exist, STARK_ERR_NO_MEM without room for the transient 5.5 kB, STARK_ERR_IO when the read-back
 * differs (the file is removed), else the storage error of the failing step; no file is left
 * behind on any error.
 */
stark_err_t selftest_export_run(const selftest_env_t *env, char *name, size_t cap, size_t *bytes);
