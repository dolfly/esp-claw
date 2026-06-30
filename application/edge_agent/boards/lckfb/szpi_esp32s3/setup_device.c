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

static i2c_master_dev_handle_t s_pca9557_handle = NULL;

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

#define BOARD_FW_VERSION "szpi_esp32s3_v0.1"

static int io_expander_init(void *cfg, int cfg_size, void **device_handle)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, " Board: szpi_esp32s3 (lckfb 立创实战派)");
    ESP_LOGI(TAG, " Firmware: " BOARD_FW_VERSION " (" __DATE__ " " __TIME__ ")");
    ESP_LOGI(TAG, "============================================");

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

    /* P0=0(LCD_CS=active), P1=1(PA_EN=high), P2=1(DVP_PWDN=HIGH=camera power-down)
     * Camera is held in power-down here; camera_power device releases it before camera init. */
    uint8_t out_buf[] = {0x01, 0x06};
    ret = i2c_master_transmit(dev_handle, out_buf, sizeof(out_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write PCA9557 output register");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    uint8_t cfg_buf[] = {0x03, 0xF8};
    ret = i2c_master_transmit(dev_handle, cfg_buf, sizeof(cfg_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write PCA9557 config register");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    s_pca9557_handle = dev_handle;
    ESP_LOGI(TAG, "PCA9557: LCD_CS=LOW(cs active), PA_EN=HIGH(amp on), DVP_PWDN=HIGH(camera power-down)");
    *device_handle = NULL;
    return 0;
}

static int io_expander_deinit(void *device_handle)
{
    (void)device_handle;
    return 0;
}

/* Read-modify-write PCA9557 output register to preserve other output bits */
static esp_err_t io_expander_set_output_bit(int bit, bool set)
{
    if (!s_pca9557_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t reg = 0x01;
    uint8_t current = 0;
    esp_err_t ret = i2c_master_transmit_receive(s_pca9557_handle, &reg, 1, &current, 1, -1);
    if (ret != ESP_OK) return ret;
    if (set) {
        current |= (uint8_t)(1 << bit);
    } else {
        current &= (uint8_t)(~(1 << bit));
    }
    uint8_t out_buf[] = {0x01, current};
    return i2c_master_transmit(s_pca9557_handle, out_buf, sizeof(out_buf), -1);
}

esp_err_t io_expander_set_pa_en(bool enable)
{
    return io_expander_set_output_bit(1, enable);
}

esp_err_t io_expander_set_dvp_pwdn(bool powerdown)
{
    return io_expander_set_output_bit(2, powerdown);
}

CUSTOM_DEVICE_IMPLEMENT(io_expander, io_expander_init, io_expander_deinit);

static int camera_power_preinit(void *config, int cfg_size, void **device_handle)
{
    (void)config;
    (void)cfg_size;

    ESP_LOGI(TAG, "Camera power-on reset: PWDN HIGH->LOW");

    io_expander_set_dvp_pwdn(true);
    vTaskDelay(pdMS_TO_TICKS(10));
    io_expander_set_dvp_pwdn(false);
    vTaskDelay(pdMS_TO_TICKS(50));

    *device_handle = NULL;
    return 0;
}

static int camera_power_deinit(void *device_handle)
{
    (void)device_handle;
    return 0;
}

CUSTOM_DEVICE_IMPLEMENT(camera_power, camera_power_preinit, camera_power_deinit);

/* QMI8658 register addresses */
#define QMI8658_WHO_AM_I    0x00
#define QMI8658_CTRL1       0x02
#define QMI8658_CTRL2       0x03
#define QMI8658_CTRL3       0x04
#define QMI8658_CTRL7       0x08
#define QMI8658_RESET       0x60

static int imu_sensor_init(void *cfg, int cfg_size, void **device_handle)
{
    const dev_custom_imu_sensor_config_t *config = (const dev_custom_imu_sensor_config_t *)cfg;
    esp_err_t ret;
    void *i2c_bus_handle = NULL;

    ret = esp_board_periph_ref_handle(config->peripheral_name, &i2c_bus_handle);
    if (ret != ESP_OK || !i2c_bus_handle) {
        ESP_LOGE(TAG, "Failed to get I2C handle for QMI8658");
        return -1;
    }

    uint16_t addr_7bit = (uint16_t)config->i2c_addr;
    ret = i2c_master_probe((i2c_master_bus_handle_t)i2c_bus_handle, addr_7bit, 100);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "QMI8658 not found at 0x%02x", addr_7bit);
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
        ESP_LOGE(TAG, "Failed to add QMI8658 to I2C bus");
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    uint8_t id = 0;
    uint8_t reg = QMI8658_WHO_AM_I;
    ret = i2c_master_transmit_receive(dev_handle, &reg, 1, &id, 1, -1);
    if (ret != ESP_OK || id != 0x05) {
        ESP_LOGW(TAG, "QMI8658 WHO_AM_I mismatch: got 0x%02x, expected 0x05", id);
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        *device_handle = NULL;
        return 0;
    }

    uint8_t reset_buf[] = {QMI8658_RESET, 0xB0};
    ret = i2c_master_transmit(dev_handle, reset_buf, sizeof(reset_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset QMI8658");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    vTaskDelay(pdMS_TO_TICKS(10));

    uint8_t ctrl1_buf[] = {QMI8658_CTRL1, 0x40};
    ret = i2c_master_transmit(dev_handle, ctrl1_buf, sizeof(ctrl1_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write QMI8658 CTRL1");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    uint8_t ctrl7_buf[] = {QMI8658_CTRL7, 0x03};
    ret = i2c_master_transmit(dev_handle, ctrl7_buf, sizeof(ctrl7_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write QMI8658 CTRL7");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    uint8_t ctrl2_buf[] = {QMI8658_CTRL2, 0x95};
    ret = i2c_master_transmit(dev_handle, ctrl2_buf, sizeof(ctrl2_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write QMI8658 CTRL2");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    uint8_t ctrl3_buf[] = {QMI8658_CTRL3, 0xD5};
    ret = i2c_master_transmit(dev_handle, ctrl3_buf, sizeof(ctrl3_buf), -1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write QMI8658 CTRL3");
        i2c_master_bus_rm_device(dev_handle);
        esp_board_periph_unref_handle(config->peripheral_name);
        return -1;
    }

    i2c_master_bus_rm_device(dev_handle);
    esp_board_periph_unref_handle(config->peripheral_name);
    ESP_LOGI(TAG, "QMI8658 initialized (ACC 4g 250Hz, GRY 512dps 250Hz)");
    *device_handle = NULL;
    return 0;
}

static int imu_sensor_deinit(void *device_handle)
{
    (void)device_handle;
    return 0;
}

CUSTOM_DEVICE_IMPLEMENT(imu_sensor, imu_sensor_init, imu_sensor_deinit);
