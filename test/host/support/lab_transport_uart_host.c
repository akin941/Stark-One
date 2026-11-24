/*
 * lab_transport_uart_host.c — the lab transport's UART group for the host (stark_lab_transport.h):
 * the same guard as lab_transport_uart.c (the hardware) and the test image's transport, in front of
 * the test-only UART peer (test/fake/fake_lab.c). A read that finds nothing waits its timeout out
 * in model time (it advances the fake peer's clock), a write waits out its wire time, so a
 * loopback or a stream behaves as it would on a wire and no test sleeps.
 */
#include <string.h>
#include "fake_lab.h"
#include "lab_transport_uart_host.h"
#include "stark_lab_transport.h"

static uint64_t s_now_us;
static bool s_installed;
static bool s_tx;
static int s_fail_after[LAB_UART_HOST_KINDS];
static bool s_fail_armed[LAB_UART_HOST_KINDS];
static unsigned s_opens;
static unsigned s_closes;

static void advance(uint64_t us)
{
    s_now_us += us;
    fake_lab_time_us(s_now_us);
}

/* True when this call is the one a fault was armed for. */
static bool hit(lab_uart_host_call_t call)
{
    if (!s_fail_armed[call]) {
        return false;
    }
    if (s_fail_after[call] > 0) {
        s_fail_after[call]--;
        return false;
    }
    return true;
}

void lab_uart_host_reset(void)
{
    s_now_us = 0;
    s_installed = false;
    s_tx = false;
    memset(s_fail_armed, 0, sizeof s_fail_armed);
    memset(s_fail_after, 0, sizeof s_fail_after);
    s_opens = 0;
    s_closes = 0;
    fake_lab_time_us(0);
}

uint64_t lab_uart_host_time_us(void)
{
    return s_now_us;
}

void lab_uart_host_set_time_us(uint64_t us)
{
    s_now_us = us;
    fake_lab_time_us(us);
}

void lab_uart_host_fail(lab_uart_host_call_t call, int after)
{
    if ((unsigned)call < (unsigned)LAB_UART_HOST_KINDS) {
        s_fail_armed[call] = after >= 0;
        s_fail_after[call] = after >= 0 ? after : 0;
    }
}

unsigned lab_uart_host_opens(void)
{
    return s_opens;
}

unsigned lab_uart_host_closes(void)
{
    return s_closes;
}

stark_err_t stark_lab_transport_uart_open(uint32_t baud, uint8_t fmt, size_t rx_buf, bool tx)
{
    if (!stark_lab_uart_args_ok(baud, fmt, rx_buf)) {
        return STARK_ERR_INVALID_ARG;
    }
    const stark_board_pins_t *pins = stark_board_pins();
    if (!stark_lab_uart_pins_ok(pins)) {
        return STARK_ERR_NOT_SUPPORTED;
    }
    if (s_installed) {
        return STARK_ERR_STATE;
    }
    if (hit(LAB_UART_HOST_OPEN)) {
        return STARK_ERR_IO;
    }
    fake_lab_uart_pins(pins->lab_uart_tx, pins->lab_uart_rx);
    stark_err_t err = fake_lab_uart_open(baud, fmt, rx_buf, tx);
    if (err == STARK_OK) {
        s_installed = true;
        s_tx = tx;
        s_opens++;
    }
    return err;
}

int stark_lab_transport_uart_read(uint8_t *buf, size_t max, uint32_t timeout_ms,
                                  stark_lab_uart_err_t *errs)
{
    if (errs != NULL) {
        *errs = (stark_lab_uart_err_t){0};
    }
    if (buf == NULL || max == 0u || max > STARK_LAB_UART_READ_MAX ||
        timeout_ms > STARK_LAB_UART_TIMEOUT_MS_MAX) {
        return -(int)STARK_ERR_INVALID_ARG;
    }
    if (!s_installed) {
        return -(int)STARK_ERR_STATE;
    }
    if (hit(LAB_UART_HOST_READ)) {
        return -(int)STARK_ERR_IO;
    }
    uint32_t fe = 0;
    uint32_t pe = 0;
    uint32_t ov = 0;
    size_t n = fake_lab_uart_read(buf, max, &fe, &pe, &ov);
    if (n == 0u && timeout_ms > 0u) {
        advance((uint64_t)timeout_ms * 1000u); /* nothing arrived: the wait ran out */
        n = fake_lab_uart_read(buf, max, &fe, &pe, &ov);
    }
    if (errs != NULL) {
        *errs = (stark_lab_uart_err_t){.framing = fe, .parity = pe, .overrun = ov};
    }
    return (int)n;
}

int stark_lab_transport_uart_write(const uint8_t *buf, size_t n, uint32_t timeout_ms)
{
    if (buf == NULL || n == 0u || n > STARK_LAB_UART_WRITE_MAX ||
        timeout_ms > STARK_LAB_UART_TIMEOUT_MS_MAX) {
        return -(int)STARK_ERR_INVALID_ARG;
    }
    if (!s_installed || !s_tx) {
        return -(int)STARK_ERR_STATE;
    }
    if (hit(LAB_UART_HOST_WRITE)) {
        return -(int)STARK_ERR_IO;
    }
    size_t sent = fake_lab_uart_write(buf, n);
    uint64_t wire_us = fake_lab_uart_wire_us(sent);
    if (wire_us > (uint64_t)timeout_ms * 1000u) {
        advance((uint64_t)timeout_ms * 1000u);
        return -(int)STARK_ERR_TIMEOUT;
    }
    advance(wire_us); /* the call returns when the last character has left */
    return (int)sent;
}

stark_err_t stark_lab_transport_uart_set_baud(uint32_t baud)
{
    if (baud < STARK_LAB_UART_BAUD_MIN || baud > STARK_LAB_UART_BAUD_MAX) {
        return STARK_ERR_INVALID_ARG;
    }
    if (!s_installed) {
        return STARK_ERR_STATE;
    }
    if (hit(LAB_UART_HOST_SET_BAUD)) {
        return STARK_ERR_IO;
    }
    return fake_lab_uart_set_baud(baud);
}

void stark_lab_transport_uart_close(void)
{
    if (s_installed) {
        fake_lab_uart_close();
        s_installed = false;
        s_tx = false;
        s_closes++;
    }
}
