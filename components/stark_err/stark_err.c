/*
 * stark_err.c — error string implementation
 * The switch is intentionally exhaustive (no default) so that adding
 * a new enum value without a corresponding case produces a compiler warning
 * (with -Wswitch-enum) or error (with -Werror=switch-enum).
 */
#include "stark_err.h"

const char *stark_err_str(stark_err_t err)
{
    switch (err) {
        case STARK_OK:              return "OK";
        case STARK_ERR_INVALID_ARG: return "Invalid argument";
        case STARK_ERR_NO_MEM:      return "Out of memory";
        case STARK_ERR_TIMEOUT:     return "Timeout";
        case STARK_ERR_NOT_FOUND:   return "Not found";
        case STARK_ERR_NOT_SUPPORTED: return "Not supported";
        case STARK_ERR_BUSY:        return "Busy";
        case STARK_ERR_IO:          return "I/O error";
        case STARK_ERR_STATE:       return "Invalid state";
    }
    /* Unreachable if all enum values are handled; GCC/Clang warn with -Wswitch-enum */
    return "Unknown error";
}