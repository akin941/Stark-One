/*
 * module_traffic.c — test image only (TASKS.md STARK-0503): while emu_mod_traffic
 * is set, a stand-in module driver claims slot 0 through the production API and
 * runs SPI transactions on MOD_CS through production stark_module_spi, alongside
 * the panel's rendering. esp-emu has nothing on the other end of MOD_CS. It shows
 * software coexistence on the emulator's bus model, never signal integrity
 * (ROADMAP V1 exit 11).
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "stark_log.h"
#include "stark_module.h"

volatile uint8_t emu_mod_traffic;

static void run_once(void)
{
    stark_module_lease_t lease;
    stark_err_t err = stark_module_claim(0, STARK_CAP_MOD_SPI, "emu", &lease);
    if (err != STARK_OK) {
        STARK_LOGI("emutraffic", "claim refused: %s", stark_err_str(err));
        return;
    }
    stark_module_spi_t *dev = NULL;
    err = stark_module_spi_attach(&lease, 5000000, 0, &dev);
    if (err != STARK_OK) {
        STARK_LOGE("emutraffic", "attach failed: %s", stark_err_str(err));
        stark_module_release(&lease);
        return;
    }
    static uint8_t tx[32];
    static uint8_t rx[32];
    unsigned ok = 0;
    while (emu_mod_traffic != 0) {
        err = stark_module_spi_xfer(dev, tx, rx, sizeof tx);
        if (err != STARK_OK) {
            break;
        }
        if (++ok == 100) {
            STARK_LOGI("emutraffic", "100 transfers ok");
        }
        vTaskDelay(1);
    }
    STARK_LOGI("emutraffic", "%u transfers, stopped: %s", ok, stark_err_str(err));
    stark_module_spi_detach(dev);
    stark_module_release(&lease);
}

static void traffic_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (emu_mod_traffic != 0) {
            run_once();
            while (emu_mod_traffic != 0) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

void module_traffic_start(void)
{
    (void)xTaskCreatePinnedToCore(traffic_task, "emutraffic", 3072, NULL, 1, NULL, 0);
}
