#include "audio_sdcard.h"

#include <stdio.h>
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "audio_sd";

static sdmmc_card_t *s_card = NULL;
static bool s_bus_initialized = false;
static bool s_mounted = false;

/* Pin layout mirrors the reference music demo */
static const spi_bus_config_t s_bus_cfg = {
    .mosi_io_num = GPIO_NUM_11,
    .miso_io_num = GPIO_NUM_13,
    .sclk_io_num = GPIO_NUM_12,
    .quadwp_io_num = GPIO_NUM_NC,
    .quadhd_io_num = GPIO_NUM_NC,
    .max_transfer_sz = 16 * 1024,
};

esp_err_t audio_sdcard_mount(void)
{
    if (s_mounted)
    {
        return ESP_OK;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    if (!s_bus_initialized)
    {
        esp_err_t bus_ret = spi_bus_initialize(host.slot, &s_bus_cfg, SDSPI_DEFAULT_DMA);
        if (bus_ret != ESP_OK && bus_ret != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "spi bus init failed: %s", esp_err_to_name(bus_ret));
            return bus_ret;
        }
        s_bus_initialized = true;
    }

    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.host_id = host.slot;
    slot_config.gpio_cs = GPIO_NUM_2;

    const esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 4 * 1024,
        .disk_status_check_enable = false,
    };

    esp_err_t mount_ret = esp_vfs_fat_sdspi_mount(AUDIO_SD_MOUNT_POINT, &host, &slot_config, &mount_config, &s_card);
    if (mount_ret != ESP_OK)
    {
        ESP_LOGE(TAG, "mount sd card failed: %s", esp_err_to_name(mount_ret));
        return mount_ret;
    }

    s_mounted = true;
    sdmmc_card_print_info(stdout, s_card);
    return ESP_OK;
}

void audio_sdcard_unmount(void)
{
    if (!s_mounted)
    {
        return;
    }

    esp_vfs_fat_sdcard_unmount(AUDIO_SD_MOUNT_POINT, s_card);
    s_card = NULL;
    s_mounted = false;
}

bool audio_sdcard_is_mounted(void)
{
    return s_mounted;
}
