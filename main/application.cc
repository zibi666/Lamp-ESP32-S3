#include "application.h"
#include "board.h"
#include "system_info.h"
#include "audio_codec.h"
#include "assets/lang_config.h"
#include "assets.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>
#include "audio_afe_ws_sender.h" // 确保包含此头文件
#include "pwm_test.h"

// 睡眠监测相关头文件 (来自 other_projects)
extern "C" {
#include "uart.h"
#include "app_controller.h"
#include "rtc_service.h"
#include "xl9555_keys.h"
#include "alarm_music.h"
}

#define TAG "Application"

static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    // Disabled in offline audio-only mode
}

void Application::CheckNewVersion() {
    xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateIdle) {
        SetDeviceState(kDeviceStateListening);
    } else {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::StartListening() {
    SetListeningMode(kListeningModeManualStop);
}

void Application::StopListening() {
    SetDeviceState(kDeviceStateIdle);
}

// ------------------------------------------------------------------------
// Start: 核心修改区域
// ------------------------------------------------------------------------
void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    auto display = board.GetDisplay();
    board.StartNetwork();
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

    // 强制设置音量为60
    Board::GetInstance().GetAudioCodec()->SetOutputVolume(60);

    // 1. 初始化 WebSocket 发送器
    audio_afe_ws_sender_init();
    
    // 【重要】注释掉 Raw PCM 发送，避免与 Opus 流混淆
    // audio_afe_ws_hook(&audio_service_); 

    // 2. 开启语音处理（AFE + 编码）
    audio_service_.EnableVoiceProcessing(true);

    AudioServiceCallbacks callbacks;
    callbacks.on_vad_change = [this](bool speaking) {
        if (speaking) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
        }
    };
    
    // 3. 将 Opus 编码队列绑定到 WebSocket 发送 (只发 Opus)
    audio_afe_ws_attach_send_callbacks(&audio_service_, callbacks);
    audio_service_.SetCallbacks(callbacks);

    // 4. 绑定下行音频：服务端推送的 Opus 数据将直接送入解码/播放队列
    audio_afe_ws_attach_downlink(&audio_service_);

    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);
    display->ShowNotification(Lang::Strings::STANDBY);
    
    // 开机成功提示音
    audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);

    // Start PWM test for IO19 and IO20
    StartPwmTest();

    // ========== 睡眠监测功能初始化 (来自 other_projects) ==========
    ESP_LOGI(TAG, "初始化睡眠监测功能...");
    
    // 初始化 UART1 用于雷达模块通信
    uart0_init(115200);
    ESP_LOGI(TAG, "UART1 已初始化用于雷达模块");

    // 初始化 XL9555 按键和蜂鸣器
    if (xl9555_keys_init() == ESP_OK) {
        ESP_LOGI(TAG, "XL9555 按键初始化成功");
        if (xl9555_beep_init() == ESP_OK) {
            xl9555_beep_off();
            ESP_LOGI(TAG, "蜂鸣器初始化成功");
        }
    } else {
        ESP_LOGW(TAG, "XL9555 初始化失败，按键和蜂鸣器功能不可用");
    }

    // 启动 RTC NTP 同步任务
    if (rtc_start_periodic_sync(10 * 60 * 1000) == ESP_OK) {
        ESP_LOGI(TAG, "RTC NTP 同步任务已启动");
    } else {
        ESP_LOGW(TAG, "RTC NTP 同步任务启动失败");
    }

    // 初始化闹钟音乐模块
    if (alarm_music_init() == ESP_OK) {
        if (alarm_music_start() == ESP_OK) {
            ESP_LOGI(TAG, "闹钟音乐任务已启动");
        }
    }

    // 启动睡眠监测业务控制任务
    if (app_controller_start() == ESP_OK) {
        ESP_LOGI(TAG, "睡眠监测业务任务已启动");
    } else {
        ESP_LOGW(TAG, "睡眠监测业务任务启动失败");
    }

    ESP_LOGI(TAG, "睡眠监测功能初始化完成");
    // ========== 睡眠监测功能初始化结束 ==========
}

void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            if (clock_ticks_ % 10 == 0) {
                SystemInfo::PrintHeapStats();
            }
        }
    }
}
// ------------------------------------------------------------------------
// End: 核心修改区域
// ------------------------------------------------------------------------


void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    SetDeviceState(kDeviceStateIdle);
}

void Application::SetListeningMode(ListeningMode mode) {
    listening_mode_ = mode;
    SetDeviceState(kDeviceStateListening);
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");
            break;
        case kDeviceStateSpeaking:
            display->SetStatus(Lang::Strings::SPEAKING);
            break;
        default:
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }
    if (!audio_service_.IsIdle()) {
        return false;
    }
    return true;
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}
