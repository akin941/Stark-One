/*
 * module_bridge.h — module state → the launcher (STARK-0502). The composition
 * root connects stark_module and stark_app; neither knows the other.
 */
#pragma once

#include "stark_err.h"

/* Subscribes; call before stark_module_init(). */
stark_err_t module_bridge_init(void);

/* Lists IR and the NFC reader and gives their state; after stark_module_init(). */
void module_bridge_start(void);
