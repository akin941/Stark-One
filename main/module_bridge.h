/*
 * module_bridge.h — module state → the launcher (STARK-0502). The composition
 * root connects stark_module and stark_app; neither knows the other.
 */
#pragma once

#include "stark_err.h"

stark_err_t module_bridge_init(void);
