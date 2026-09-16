/*
 * stark_err.h — L1 error codes, header-only, no dependencies
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

/**
 * STARK error codes.
 * Values are stable and must not be reordered.
 */
typedef enum {
    STARK_OK = 0,
    STARK_ERR_INVALID_ARG,
    STARK_ERR_NO_MEM,
    STARK_ERR_TIMEOUT,
    STARK_ERR_NOT_FOUND,
    STARK_ERR_NOT_SUPPORTED,
    STARK_ERR_BUSY,
    STARK_ERR_IO,
    STARK_ERR_STATE,
} stark_err_t;

/**
 * Returns a human-readable string for the given error code.
 * The returned string is statically allocated and must not be freed.
 */
const char *stark_err_str(stark_err_t err);

/**
 * Checks a condition and returns the error code if false.
 * Usage: STARK_CHECK(ptr != NULL, STARK_ERR_INVALID_ARG);
 */
#define STARK_CHECK(cond, err)                                                                     \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            return (err);                                                                          \
        }                                                                                          \
    } while (0)

/**
 * Evaluates an expression that returns stark_err_t and returns early on error.
 * Usage: STARK_CHECK_RET(stark_something());
 */
#define STARK_CHECK_RET(expr)                                                                      \
    do {                                                                                           \
        stark_err_t _err = (expr);                                                                 \
        if (_err != STARK_OK) {                                                                    \
            return _err;                                                                           \
        }                                                                                          \
    } while (0)
