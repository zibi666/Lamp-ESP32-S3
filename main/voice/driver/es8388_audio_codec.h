#ifndef _ES8388_AUDIO_CODEC_H
#define _ES8388_AUDIO_CODEC_H

#include "audio_codec.h"

#include <driver/i2c_master.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mutex>
#include <atomic>


class Es8388AudioCodec : public AudioCodec {
private:
    const audio_codec_data_if_t* data_if_ = nullptr;
    const audio_codec_ctrl_if_t* ctrl_if_ = nullptr;
    const audio_codec_if_t* codec_if_ = nullptr;
    const audio_codec_gpio_if_t* gpio_if_ = nullptr;

    esp_codec_dev_handle_t output_dev_ = nullptr;
    esp_codec_dev_handle_t input_dev_ = nullptr;
    gpio_num_t pa_pin_ = GPIO_NUM_NC;
    // 使用独立的 mutex 分别保护输入和输出操作，避免 Read() 阻塞 Write()/EnableOutput()
    std::mutex input_mutex_;   // 保护 input_dev_ 操作
    std::mutex output_mutex_;  // 保护 output_dev_ 操作
    // 保留旧名称以兼容，映射到 output_mutex_（EnableInput/EnableOutput 使用）
    std::mutex& data_if_mutex_ = output_mutex_;
    i2s_std_config_t i2s_std_cfg_ = {};
    bool i2s_std_cfg_inited_ = false;
    bool saved_input_enabled_ = false;
    int external_sample_rate_ = 0;  // 外部播放时的采样率，0表示未配置
    std::atomic<bool> input_close_pending_{false};  // 当无法获取锁时，标记待关闭，由 Read() 执行

    void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din);
    esp_err_t ReconfigureI2sTx(int sample_rate, int channels);

    virtual int Read(int16_t* dest, int samples) override;
    virtual int Write(const int16_t* data, int samples) override;

public:
    Es8388AudioCodec(void* i2c_master_handle, i2c_port_t i2c_port, int input_sample_rate, int output_sample_rate,
        gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, gpio_num_t din,
        gpio_num_t pa_pin, uint8_t es8388_addr, bool input_reference = false);
    virtual ~Es8388AudioCodec();

    void SetOutputVolumeRuntime(int volume);
    bool BeginExternalPlayback(int sample_rate, int channels);
    void EndExternalPlayback();
    virtual void SetOutputVolume(int volume) override;
    virtual void EnableInput(bool enable) override;
    virtual void EnableOutput(bool enable) override;
};

#endif // _ES8388_AUDIO_CODEC_H
