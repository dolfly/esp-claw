/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <string.h>
#include "esp_log.h"
#include "esp_check.h"
#include "esp_board_manager_includes.h"
#include "gen_board_device_custom.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_st7789.h"
#if __has_include(<esp_lcd_touch_ft5x06.h>)
#define HAS_FT5X06  1
#include "esp_lcd_touch_ft5x06.h"
#endif

static const char *TAG = "SZPI_ESP32S3_SETUP";

esp_err_t lcd_panel_factory_entry_t(esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_panel_dev_config_t *panel_dev_config,
                                    esp_lcd_panel_handle_t *ret_panel)
{
    esp_err_t ret = esp_lcd_new_panel_st7789(io, panel_dev_config, ret_panel);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "New ST7789 panel failed: %s", esp_err_to_name(ret));
    }
    return ret;
}

#if defined(HAS_FT5X06)
esp_err_t lcd_touch_factory_entry_t(const esp_lcd_panel_io_handle_t io,
                                    const esp_lcd_touch_config_t *config,
                                    esp_lcd_touch_handle_t *tp)
{
    esp_err_t ret = esp_lcd_touch_new_i2c_ft5x06(io, config, tp);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create FT6x06 touch: %s", esp_err_to_name(ret));
    }
    return ret;
}
#endif

static int io_expander_init(void *cfg, int cfg_size, void **device_handle)
{
    const dev_custom_io_expander_config_t *config = (const dev_custom_io_expander_config_t *)cfg;
    esp_err_t ret;
    void *i2c_bus_handle = NULL;

    ret = esp_board_periph_ref_handle(config->peripheral_name, &i2c_bus_handle);
    if (ret != ESP_OK || !i2c_bus_handle) {
        ESP_LOGE(TAG, "Failed to get I2C handle for PCA9557");
        return -1;
    }

    uint16_t addr_7bit = (uint16_t)(config->i2c_addr >> 1);
    ret = i2c_master_probe((i2c_master_bus_handle_t)i2c_bus_handle, addr_7bit, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "PCA9557 not found at 0x%02x, camera power-down may not be controlled", addr_7bit);
        esp_board_periph_unref_handle(config->peripheral_name);
        *device_handle = NULL;
        return 0;
    }

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr_7bit,
        .scl_speed_hz = (uint32_t)config->frequency,
    };
    i2c_master_dev_handle_t dev_handle;
    ret = i2c_master_bus_add_device((i2c_master_bus_handle_t)i2c_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add PCA9557 to I2C bus");
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    uint8_t out_buf[] = {0x01, 0x00};
    ret = i2c_master_transmit(dev_handle, out_buf, sizeof(out_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write PCA9557 output register");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    uint8_t cfg_buf[] = {0x03, 0xFB};
    ret = i2c_master_transmit(dev_handle, cfg_buf, sizeof(cfg_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write PCA9557 config register");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    i2c_master_bus_rm_device(dev_handle);
    esp_board_periph_unref_handle(config->peripheral_name);
    ESP_LOGI(TAG, "PCA9557: IO2(DVP_PWDN) LOW, camera powered on");
    *device_handle = NULL;
    return 0;
}

static int io_expander_deinit(void *device_handle)
{
    (void)device_handle;
    return 0;
}

CUSTOM_DEVICE_IMPLEMENT(io_expander, io_expander_init, io_expander_deinit);
