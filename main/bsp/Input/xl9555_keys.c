#include "xl9555_keys.h"

#include "audio_hw.h"
#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include <stdbool.h>

static const char *TAG = "xl9555_keys";

#define XL9555_ADDR 0x20
#define XL9555_I2C_CLK_HZ 400000

/* XL9555 register addresses */
#define REG_INPUT0   0x00
#define REG_INPUT1   0x01
#define REG_OUTPUT0  0x02
#define REG_OUTPUT1  0x03
#define REG_CONFIG0  0x06
#define REG_CONFIG1  0x07

/* Bit positions for keys on port1 */
#define KEY0_BIT 7  /* IO1_7 */
#define KEY1_BIT 6  /* IO1_6 */
#define KEY2_BIT 5  /* IO1_5 */
#define KEY3_BIT 4  /* IO1_4 */

/* Port0 bit 2 drives SPK_EN (active low to enable speaker) on this board */
#define SPK_EN_BIT 2
/* Port0 bit 3 drives BEEP (active low to enable buzzer) */
#define BEEP_BIT   3

static i2c_master_dev_handle_t s_dev = NULL;

static esp_err_t xl9555_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_dev, buf, sizeof(buf), 1000);
}

static esp_err_t xl9555_read_regs(uint8_t reg, uint8_t *data, size_t len);

static esp_err_t xl9555_update_output_bit(uint8_t bit, bool level)
{
    uint8_t out[2];
    if (xl9555_read_regs(REG_OUTPUT0, out, sizeof(out)) != ESP_OK) {
        return ESP_FAIL;
    }

    if (bit < 8) {
        if (level) out[0] |= (uint8_t)(1u << bit); else out[0] &= (uint8_t)~(1u << bit);
    } else {
        uint8_t b = bit - 8;
        if (level) out[1] |= (uint8_t)(1u << b); else out[1] &= (uint8_t)~(1u << b);
    }

    if (xl9555_write_reg(REG_OUTPUT0, out[0]) != ESP_OK) {
        return ESP_FAIL;
    }
    return xl9555_write_reg(REG_OUTPUT1, out[1]);
}

static esp_err_t xl9555_read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, 1000);
}

esp_err_t xl9555_keys_init(void)
{
    /* 首先初始化音频硬件（建立I2C总线） */
    esp_err_t ret = audio_hw_i2c_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "audio_hw I2C init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_master_bus_handle_t i2c_bus = audio_hw_get_i2c_bus();
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "Failed to get I2C bus from audio_hw");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_dev == NULL) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = XL9555_ADDR,
            .scl_speed_hz = XL9555_I2C_CLK_HZ,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &dev_cfg, &s_dev), TAG, "add xl9555");
    }

    /* Configure: port0 bit2 as output for speaker enable, others inputs; port1 all inputs */
    uint8_t cfg0 = 0xFF & ~( (1u << SPK_EN_BIT) | (1u << BEEP_BIT) ); /* 0=output */
    ESP_RETURN_ON_ERROR(xl9555_write_reg(REG_CONFIG0, cfg0), TAG, "cfg0");
    ESP_RETURN_ON_ERROR(xl9555_write_reg(REG_CONFIG1, 0xFF), TAG, "cfg1");

    /* Drive speaker enable low to turn on amplifier, keep beep high (off) */
    uint8_t out0 = 0xFF & ~(1u << SPK_EN_BIT); /* SPK_EN=0 (on), BEEP=1 (off) */
    ESP_RETURN_ON_ERROR(xl9555_write_reg(REG_OUTPUT0, out0), TAG, "spk on");
    ESP_RETURN_ON_ERROR(xl9555_write_reg(REG_OUTPUT1, 0xFF), TAG, "out1");
    return ESP_OK;
}

esp_err_t xl9555_beep_init(void)
{
    /* already configured in keys_init */
    return (s_dev != NULL) ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t xl9555_beep_on(void)
{
    return xl9555_update_output_bit(BEEP_BIT, false); /* active low */
}

esp_err_t xl9555_beep_off(void)
{
    return xl9555_update_output_bit(BEEP_BIT, true);
}

uint8_t xl9555_keys_scan(uint8_t mode)
{
    static uint8_t key_up = 1;
    uint8_t keyval = XL9555_KEY_NONE;

    if (mode)
    {
        key_up = 1;
    }

    uint8_t in[2] = {0};
    if (s_dev == NULL || xl9555_read_regs(REG_INPUT0, in, sizeof(in)) != ESP_OK)
    {
        return XL9555_KEY_NONE;
    }

    /* Active low */
    bool k0 = !(in[1] & (1u << KEY0_BIT));
    bool k1 = !(in[1] & (1u << KEY1_BIT));
    bool k2 = !(in[1] & (1u << KEY2_BIT));
    bool k3 = !(in[1] & (1u << KEY3_BIT));

    if (key_up && (k0 || k1 || k2 || k3))
    {
        key_up = 0;
        if (k0) keyval = XL9555_KEY0;
        else if (k1) keyval = XL9555_KEY1;
        else if (k2) keyval = XL9555_KEY2;
        else if (k3) keyval = XL9555_KEY3;
    }
    else if (!k0 && !k1 && !k2 && !k3)
    {
        key_up = 1;
    }

    return keyval;
}
