/*
 * ui_port_spi.c — the host UI port's lab service, SPI group (ui_port_spi.h): stark_lab.h's
 * stark_lab_spi_*() over lab_spi.c, the module core and the fake SPI devices. It mirrors
 * stark_lab.c (components/stark_lab), which adds the lock, the real clock, the module service and
 * the event bus; the decisions and the log lines are the same.
 */
#include "ui_port_spi.h"
#include <string.h>
#include "fake_24c02.h"
#include "fake_module_catalog.h"
#include "lab_spi.h"
#include "module_core.h"
#include "stark_event.h"
#include "stark_log.h"

#define A0 0x50
#define A1 0x51

/* HARDWARE.md §2's map as far as the SPI rules read it: slot 0 has a chip select, slot 1 none. */
static const stark_board_pins_t k_map = {
    .lab_uart_tx = 38,
    .lab_uart_rx = 39,
    .module = {{.cs = 40, .irq0 = 41, .irq1 = 42, .en = -1, .eeprom_addr = A0},
               {.cs = -1, .irq0 = -1, .irq1 = -1, .en = -1, .eeprom_addr = A1}},
};
static const module_wiring_t k_wires[] = {{A0, 40, 41, 42}, {A1, -1, -1, -1}};

static module_core_t s_mod;
static lab_spi_t s_spi;
static uint32_t s_now_ms;
static unsigned s_dones, s_claims, s_releases;
static stark_module_lease_t s_held;
static bool s_holding;

const stark_board_pins_t *stark_board_pins(void)
{
    return &k_map;
}

/* What the transport's host shim reads. */
size_t stark_module_list(stark_module_entry_t *out, size_t max)
{
    return module_core_list(&s_mod, out, max);
}

bool stark_module_lease_valid(const stark_module_lease_t *l)
{
    return module_core_lease_valid(&s_mod, l);
}

static stark_err_t mio_probe(void *ctx, uint8_t addr)
{
    (void)ctx;
    return fake_24c02_probe(addr);
}

static stark_err_t mio_read(void *ctx, uint8_t addr, uint8_t off, uint8_t *buf, size_t len)
{
    (void)ctx;
    return fake_24c02_read_at(addr, off, buf, len);
}

static const module_io_t k_mio = {mio_probe, mio_read, NULL, NULL, NULL};

static size_t io_list(void *ctx, stark_module_entry_t *out, size_t max)
{
    (void)ctx;
    return module_core_list(&s_mod, out, max);
}

static stark_err_t io_claim(void *ctx, uint8_t slot, stark_module_lease_t *out)
{
    (void)ctx;
    s_claims++;
    return module_core_claim(&s_mod, slot, STARK_CAP_MOD_SPI, LAB_SPI_OWNER, out);
}

static void io_release(void *ctx, const stark_module_lease_t *l)
{
    (void)ctx;
    s_releases++;
    module_core_release(&s_mod, l);
}

static stark_err_t io_xfer(void *ctx, const stark_module_lease_t *l, uint32_t hz, uint8_t mode,
                           const uint8_t *tx, uint8_t *rx, size_t n)
{
    (void)ctx;
    return stark_lab_transport_spi_xfer(l, hz, mode, tx, rx, n);
}

static uint32_t io_now(void *ctx)
{
    (void)ctx;
    return s_now_ms;
}

static void io_log(void *ctx, bool warn, const char *text)
{
    (void)ctx;
    if (warn) {
        STARK_LOGW("lab", "%s", text);
    } else {
        STARK_LOGI("lab", "%s", text);
    }
}

static void io_done(void *ctx)
{
    (void)ctx;
    s_dones++;
}

void ui_port_spi_reset(void)
{
    fake_lab_reset();
    fake_24c02_reset();
    module_core_init(&s_mod, &k_mio, k_wires, 2);
    (void)module_core_tick(&s_mod);
    (void)module_core_tick(&s_mod);
    s_now_ms = 0;
    s_dones = 0;
    s_claims = 0;
    s_releases = 0;
    s_holding = false;
    const lab_spi_io_t io = {.list = io_list,
                             .claim = io_claim,
                             .release = io_release,
                             .xfer = io_xfer,
                             .now_ms = io_now,
                             .log = io_log,
                             .done = io_done};
    lab_spi_init(&s_spi, &io, &k_map);
}

void ui_port_spi_module(unsigned slot, int id)
{
    uint8_t addr = slot == 0u ? A0 : A1;
    if (id == 0) {
        fake_24c02_remove(addr);
    } else {
        fake_24c02_put(addr, k_fake_modules[id - 1].image, MODULE_DESC_SIZE);
    }
    for (int i = 0; i < 3; i++) {
        (void)module_core_tick(&s_mod);
    }
    stark_event_t e = {.type = STARK_EVT_SYSTEM}; /* the module service tells the bus */
    e.system.id = STARK_SYS_MODULE;
    (void)stark_event_publish(&e);
}

void ui_port_spi_hold(bool held)
{
    if (held && !s_holding) {
        s_holding = module_core_claim(&s_mod, 0, STARK_CAP_MOD_SPI, "driver", &s_held) == STARK_OK;
    } else if (!held && s_holding) {
        module_core_release(&s_mod, &s_held);
        s_holding = false;
    }
}

void ui_port_spi_advance_ms(uint32_t ms)
{
    s_now_ms += ms;
}

unsigned ui_port_spi_dones(void)
{
    return s_dones;
}

bool ui_port_spi_armed(void)
{
    return lab_spi_armed(&s_spi);
}

unsigned ui_port_spi_claims(void)
{
    return s_claims;
}

unsigned ui_port_spi_releases(void)
{
    return s_releases;
}

/* ---- stark_lab.h ---------------------------------------------------------------------------- */

size_t stark_lab_spi_slots(stark_lab_spi_slot_t *out, size_t max)
{
    return out == NULL ? 0u : lab_spi_slots(&s_spi, out, max);
}

stark_err_t stark_lab_spi_request(uint8_t slot, uint32_t hz, uint8_t mode, const uint8_t *tx,
                                  size_t n, stark_lab_spi_req_t *out)
{
    return lab_spi_request(&s_spi, slot, hz, mode, tx, n, out);
}

stark_err_t stark_lab_spi_send(const stark_lab_spi_req_t *req, uint8_t *rx)
{
    return lab_spi_send(&s_spi, req, rx);
}

void stark_lab_spi_cancel(void)
{
    lab_spi_cancel(&s_spi);
}
