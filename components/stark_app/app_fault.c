/*
 * app_fault.c — the fault generation filter (app_internal.h). Pure,
 * host-tested (test/host/test_app_fault.c).
 */
#include "app_internal.h"

bool app_fault_accept(uint32_t fault_gen, uint32_t current_gen, bool app_running)
{
    return app_running && fault_gen == current_gen;
}
