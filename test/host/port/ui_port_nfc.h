/*
 * ui_port_nfc.h — host UI test port: a stark_nfc backend the test drives
 * (TASKS.md STARK-0404): module state, the tag snapshot, polling, writes.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include "stark_nfc.h"

void ui_port_nfc_reset(void);
void ui_port_nfc_set_module(stark_nfc_module_t m); /* publishes MODULE */
/* The tag in front of the antenna (NULL: none); publishes TAG or LEFT. */
void ui_port_nfc_set_tag(const stark_nfc_read_t *r);
bool ui_port_nfc_polling(void);
int ui_port_nfc_writes(void);
const uint8_t *ui_port_nfc_last_write(size_t *len);
/* What the next stark_nfc_write_ndef() does: STARK_OK / an error publishes
 * WRITE_DONE / WRITE_FAILED at once; refuse != STARK_OK is returned instead. */
void ui_port_nfc_write_outcome(stark_err_t result, stark_err_t refuse);
