#include "es8388_audio_codec.h"

#include <esp_log.h>
#include <algorithm>
#include <cmath>

#define TAG "Es8388AudioCodec"

static int MapUserVolumeToCodec(int volume) {
    int clamped = std::max(0, std::min(100, volume));
    float normalized = static_cast<float>(clamped) / 100.0f;
    float mapped = powf(normalized, 0.6f);
    int scaled = static_cast<int>(mapped * 100.0f + 0.5f);
    return std::max(0, std::min(100, scaled));
}

esp_err_t Es8388AudioCodec::ReconfigureI2sTx(int sample_rate, int channels) {
    if (sample_rate <= 0 || (channels != 1 && channels != 2)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!i2s_std_cfg_inited_ || !tx_handle_) {
        return ESP_ERR_INVALID_STATE;
    }

    i2s_std_clk_config_t clk_cfg = i2s_std_cfg_.clk_cfg;
    clk_cfg.sample_rate_hz = (uint32_t)sample_rate;

    i2s_std_slot_config_t slot_cfg = i2s_std_cfg_.slot_cfg;
    slot_cfg.slot_mode = (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    slot_cfg.slot_mask = (channels == 1) ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_BOTH;

    esp_err_t err = i2s_channel_disable(tx_handle_);
    // Ignore "not enabled yet" error
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    err = i2s_channel_reconfig_std_slot(tx_handle_, &slot_cfg);
    if (err != ESP_OK) {
        return err;
    }

    err = i2s_channel_reconfig_std_clock(tx_handle_, &clk_cfg);
    if (err != ESP_OK) {
        return err;
    }

    // Also update RX clock to avoid conflict in duplex mode
    if (rx_handle_) {
        // Ensure RX is disabled before reconfig (it should be, but just in case)
        i2s_channel_disable(rx_handle_);
        i2s_channel_reconfig_std_clock(rx_handle_, &clk_cfg);
        // Re-enable RX channel after reconfiguration
        i2s_channel_enable(rx_handle_);
    }

    err = i2s_channel_enable(tx_handle_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }

    return ESP_OK;
}

Es8388AudioCodec::Es8388AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
    gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
    gpio_num_t pa_pin, uint8_t es8388_addr, bool input_reference) {
    duplex_ = true; // 是否双工
    input_reference_ = input_reference; // 是否使用参考输入，实现回声消除
    input_channels_ = input_reference_ ? 2 : 1; // 输入通道数
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;
    pa_pin_ = pa_pin;
    CreateDuplexChannels(mclk, bclk, ws, dout, din);

    // Do initialize of related interface: data_if, ctrl_if and gpio_if
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .rx_handle = rx_handle_,
        .tx_handle = tx_handle_,
    };
    data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
    assert(data_if_ != NULL);

    // Output
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = i2c_port,
        .addr = es8388_addr,
        .bus_handle = i2c_master_handle,
    };
    ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    assert(ctrl_if_ != NULL);

    gpio_if_ = audio_codec_new_gpio();
    assert(gpio_if_ != NULL);

    es8388_codec_cfg_t es8388_cfg = {};
    es8388_cfg.ctrl_if = ctrl_if_;
    es8388_cfg.gpio_if = gpio_if_;
    es8388_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es8388_cfg.master_mode = true;
    es8388_cfg.pa_pin = pa_pin;
    es8388_cfg.pa_reverted = false;
    es8388_cfg.hw_gain.pa_voltage = 5.0;
    es8388_cfg.hw_gain.codec_dac_voltage = 3.3;
    codec_if_ = es8388_codec_new(&es8388_cfg);
    assert(codec_if_ != NULL);

    esp_codec_dev_cfg_t outdev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if_,
        .data_if = data_if_,
    };
    output_dev_ = esp_codec_dev_new(&outdev_cfg);
    assert(output_dev_ != NULL);

    esp_codec_dev_cfg_t indev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
        .codec_if = codec_if_,
        .data_if = data_if_,
    };
    input_dev_ = esp_codec_dev_new(&indev_cfg);
    assert(input_dev_ != NULL);
    esp_codec_set_disable_when_closed(output_dev_, false);
    esp_codec_set_disable_when_closed(input_dev_, false);
    ESP_LOGI(TAG, "Es8388AudioCodec initialized");
}

Es8388AudioCodec::~Es8388AudioCodec() {
    ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
    esp_codec_dev_delete(output_dev_);
    ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    esp_codec_dev_delete(input_dev_);

    audio_codec_delete_codec_if(codec_if_);
    audio_codec_delete_ctrl_if(ctrl_if_);
    audio_codec_delete_gpio_if(gpio_if_);
    audio_codec_delete_data_if(data_if_);
}

bool Es8388AudioCodec::BeginExternalPlayback(int sample_rate, int channels) {
    ESP_LOGI("Es8388", "BeginExternalPlayback: sample_rate=%d, channels=%d (system: %d)", 
             sample_rate, channels, output_sample_rate_);
    
    if (sample_rate <= 0 || (channels != 1 && channels != 2)) {
        ESP_LOGE("Es8388", "BeginExternalPlayback: invalid params");
        return false;
    }

    // 保存当前输入状态，播放结束后恢复
    saved_input_enabled_ = input_enabled_;
    
    // 如果采样率不匹配，需要重新配置 I2S
    if (sample_rate != output_sample_rate_) {
        ESP_LOGI("Es8388", "BeginExternalPlayback: reconfiguring I2S from %d to %d Hz", 
                 output_sample_rate_, sample_rate);
        
        // 先禁用输入，避免采样率变化影响 AFE 音频处理
        if (input_enabled_) {
            ESP_LOGI("Es8388", "BeginExternalPlayback: disabling input for sample rate change");
            EnableInput(false);
        }
        
        // 重新配置 I2S TX 为新采样率
        esp_err_t err = ReconfigureI2sTx(sample_rate, channels);
        if (err != ESP_OK) {
            ESP_LOGE("Es8388", "BeginExternalPlayback: failed to reconfigure I2S: %s", esp_err_to_name(err));
            // 恢复输入并返回失败
            if (saved_input_enabled_) {
                EnableInput(true);
            }
            return false;
        }
        ESP_LOGI("Es8388", "BeginExternalPlayback: I2S reconfigured successfully");
    }
    
    // 确保输出被启用
    if (!output_enabled_) {
        ESP_LOGI("Es8388", "BeginExternalPlayback: enabling output");
        EnableOutput(true);
    }
    
    external_sample_rate_ = sample_rate;
    output_channels_ = channels;
    ESP_LOGI("Es8388", "BeginExternalPlayback: ready for playback at %d Hz", sample_rate);
    return true;
}

void Es8388AudioCodec::EndExternalPlayback() {
    ESP_LOGI("Es8388", "EndExternalPlayback: external_sample_rate=%d, system=%d", 
             external_sample_rate_, output_sample_rate_);
    
    // 如果曾经切换过采样率，恢复系统原始采样率
    if (external_sample_rate_ != 0 && external_sample_rate_ != output_sample_rate_) {
        ESP_LOGI("Es8388", "EndExternalPlayback: restoring I2S to %d Hz", output_sample_rate_);
        
        esp_err_t err = ReconfigureI2sTx(output_sample_rate_, 1);
        if (err != ESP_OK) {
            ESP_LOGE("Es8388", "EndExternalPlayback: failed to restore I2S: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI("Es8388", "EndExternalPlayback: I2S restored successfully");
        }
    }
    
    // 恢复输入（如果之前是启用的）
    if (saved_input_enabled_ && !input_enabled_) {
        ESP_LOGI("Es8388", "EndExternalPlayback: restoring input");
        EnableInput(true);
    }
    
    external_sample_rate_ = 0;
    output_channels_ = 1;
    ESP_LOGI("Es8388", "EndExternalPlayback: done");
}

void Es8388AudioCodec::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din){
    assert(input_sample_rate_ == output_sample_rate_);

    i2s_chan_config_t chan_cfg = {
        .id = I2S_NUM_0,
        .role = I2S_ROLE_MASTER,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .auto_clear_after_cb = true,
        .auto_clear_before_cb = false,
        .intr_priority = 0,
    };
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = (uint32_t)output_sample_rate_,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .ext_clk_freq_hz = 0,
            .mclk_multiple = I2S_MCLK_MULTIPLE_256
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode = I2S_SLOT_MODE_STEREO,
            .slot_mask = I2S_STD_SLOT_BOTH,
            .ws_width = I2S_DATA_BIT_WIDTH_16BIT,
            .ws_pol = false,
            .bit_shift = true,
            .left_align = true,
            .big_endian = false,
            .bit_order_lsb = false
        },
        .gpio_cfg = {
            .mclk = mclk,
            .bclk = bclk,
            .ws = ws,
            .dout = dout,
            .din = din,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false
            }
        }
    };

    i2s_std_cfg_ = std_cfg;
    i2s_std_cfg_inited_ = true;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_LOGI(TAG, "Duplex channels created");
}

void Es8388AudioCodec::SetOutputVolume(int volume) {
    int mapped_volume = MapUserVolumeToCodec(volume);
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, mapped_volume));
    AudioCodec::SetOutputVolume(volume);
}

void Es8388AudioCodec::SetOutputVolumeRuntime(int volume) {
    int mapped_volume = MapUserVolumeToCodec(volume);
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_set_out_vol(output_dev_, mapped_volume));
}

void Es8388AudioCodec::EnableInput(bool enable) {
    ESP_LOGI("Es8388", "EnableInput: enable=%d, current input_enabled=%d", enable, input_enabled_);
    
    // 使用超时锁，避免被 Read() 长时间阻塞
    // Read() 在等待 I2S DMA 数据时会持有 input_mutex_ 较长时间
    const int max_retries = 50;  // 最多重试50次
    const int retry_delay_ms = 10;  // 每次重试间隔10ms
    bool lock_acquired = false;
    
    for (int i = 0; i < max_retries && !lock_acquired; i++) {
        lock_acquired = input_mutex_.try_lock();
        if (!lock_acquired) {
            if (i == 0) {
                ESP_LOGW("Es8388", "EnableInput: waiting for mutex (held by Read)");
            }
            vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        }
    }
    
    if (!lock_acquired) {
        ESP_LOGE("Es8388", "EnableInput: failed to acquire mutex after %dms", 
                 max_retries * retry_delay_ms);
        if (!enable) {
            // 无法获取锁时，设置待关闭标志，由 Read() 在安全时关闭设备
            ESP_LOGW("Es8388", "EnableInput: deferring device close to Read()");
            input_close_pending_.store(true);
            // 更新状态标志，这样 Read() 会在下次调用后返回静音数据
            AudioCodec::EnableInput(false);
        }
        return;
    }
    
    // 使用 RAII 确保解锁
    std::lock_guard<std::mutex> lock(input_mutex_, std::adopt_lock);
    ESP_LOGI("Es8388", "EnableInput: mutex acquired");
    
    if (enable == input_enabled_) {
        ESP_LOGI("Es8388", "EnableInput: already in target state");
        return;
    }
    if (enable) {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = (uint8_t) input_channels_,
            .channel_mask = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
            .sample_rate = (uint32_t)input_sample_rate_,
            .mclk_multiple = 0,
        };
        if (input_reference_) {
            fs.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
        }
        ESP_ERROR_CHECK(esp_codec_dev_open(input_dev_, &fs));
        if (input_reference_) {
            uint8_t gain = (11 << 4) + 0;
            ctrl_if_->write_reg(ctrl_if_, 0x09, 1, &gain, 1);
        }else{
            ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(input_dev_, 24.0));
        }
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(input_dev_));
    }
    AudioCodec::EnableInput(enable);
    ESP_LOGI("Es8388", "EnableInput: done, input_enabled=%d", input_enabled_);
}

void Es8388AudioCodec::EnableOutput(bool enable) {
    ESP_LOGI("Es8388", "EnableOutput: enable=%d, current output_enabled=%d", enable, output_enabled_);
    
    // 由于 Read() 使用 input_mutex_，Write()/EnableOutput 使用 output_mutex_
    // 输入和输出操作完全独立，不会相互阻塞
    std::lock_guard<std::mutex> lock(output_mutex_);
    
    if (enable == output_enabled_) {
        ESP_LOGI("Es8388", "EnableOutput: already in target state, returning");
        return;
    }
    if (enable) {
        ESP_LOGI("Es8388", "EnableOutput: opening codec dev with sample_rate=%d", output_sample_rate_);
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .channel_mask = 0,
            .sample_rate = (uint32_t)output_sample_rate_,
            .mclk_multiple = 0,
        };
        ESP_ERROR_CHECK(esp_codec_dev_open(output_dev_, &fs));
        int mapped_volume = MapUserVolumeToCodec(output_volume_);
        ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(output_dev_, mapped_volume));

        // Set analog output volume to 0dB, default is -45dB
        // 0x1E = 30 (0dB)
        uint8_t reg_val = 30; 
        if(input_reference_){
            reg_val = 27;
        }
        
        // 1. 设置音量 (原代码)
        // LOUT2/ROUT2 的音量寄存器是 48 和 49
        uint8_t regs[] = { 46, 47, 48, 49 }; // HP_LVOL, HP_RVOL, SPK_LVOL, SPK_RVOL
        for (uint8_t reg : regs) {
            ctrl_if_->write_reg(ctrl_if_, reg, 1, &reg_val, 1);
        }

        // ================================================================
        // 【新增代码】: 强制开启 LOUT2/ROUT2 输出通道
        // Register 0x04 是 DAC Power Control
        // 值 0x3C (二进制 0011 1100) 代表同时开启 LOUT1, ROUT1, LOUT2, ROUT2
        // ================================================================
        uint8_t power_reg_val = 0x3C; 
        ctrl_if_->write_reg(ctrl_if_, 0x04, 1, &power_reg_val, 1);
        
        // 额外保险：确保 DAC Control 寄存器没有静音 LOUT2
        // 虽然通常由音量寄存器控制，但防止某些默认配置置位
        // Register 0x02 (DAC Control 1)
        // uint8_t dac_ctrl_val = 0x00; 
        // ctrl_if_->write_reg(ctrl_if_, 0x02, 1, &dac_ctrl_val, 1);

        if (pa_pin_ != GPIO_NUM_NC) {
            gpio_set_level(pa_pin_, 1);
        }
    } else {
        ESP_ERROR_CHECK(esp_codec_dev_close(output_dev_));
        if (pa_pin_ != GPIO_NUM_NC) {
            gpio_set_level(pa_pin_, 0);
        }
    }
    AudioCodec::EnableOutput(enable);
}

int Es8388AudioCodec::Read(int16_t* dest, int samples) {
    if (input_enabled_) {
        // 使用 input_mutex_ 保护输入操作，不会阻塞输出相关操作
        std::lock_guard<std::mutex> lock(input_mutex_);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t)));
        
        // 检查是否有待处理的设备关闭请求（由 EnableInput 超时设置）
        if (input_close_pending_.load()) {
            ESP_LOGI("Es8388", "Read: handling deferred device close");
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_close(input_dev_));
            input_close_pending_.store(false);
        }
    } else {
        // 如果输入被禁用，模拟音频时序并填充静音数据
        // 防止上层任务（如 AudioService）进入 busy loop 导致看门狗超时
        if (samples > 0 && input_sample_rate_ > 0) {
            int duration_ms = samples * 1000 / input_sample_rate_;
            if (duration_ms < 1) duration_ms = 1;
            vTaskDelay(pdMS_TO_TICKS(duration_ms));
            memset(dest, 0, samples * sizeof(int16_t));
        }
    }
    return samples;
}

int Es8388AudioCodec::Write(const int16_t* data, int samples) {
    if (output_enabled_ && output_dev_ && data != nullptr) {
        std::lock_guard<std::mutex> lock(data_if_mutex_);
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t)));
    } else {
        static int warn_count = 0;
        if (warn_count < 5) {
            ESP_LOGW("Es8388", "Write skipped: output_enabled=%d, output_dev=%p, data=%p, samples=%d",
                     output_enabled_, output_dev_, data, samples);
            warn_count++;
        }
    }
    return samples;
}
