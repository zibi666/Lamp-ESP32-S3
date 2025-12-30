#ifndef BOARD_H
#define BOARD_H

#include <http.h>
#include <web_socket.h>
#include <udp.h>
#include <string>
#include <network_interface.h>

#include "backlight.h"
#include "assets.h"


void* create_board();
class AudioCodec;

class Led {
public:
    virtual ~Led() = default;
    virtual void OnStateChanged() {}
};

class Display {
public:
    virtual ~Display() = default;
    virtual void SetChatMessage(const std::string& sender, const std::string& message) {}
    virtual void SetEmotion(const std::string& emotion) {}
    virtual void SetStatus(const std::string& status) {}
    virtual void UpdateStatusBar(bool force = false) {}
    virtual void ShowNotification(const std::string& message, int duration_ms = 0) {}
    virtual int width() { return 0; }
    virtual int height() { return 0; }
};

class Board {
private:
    Board(const Board&) = delete; // 禁用拷贝构造函数
    Board& operator=(const Board&) = delete; // 禁用赋值操作

protected:
    Board();
    std::string GenerateUuid();

    // 软件生成的设备唯一标识
    std::string uuid_;

public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    virtual ~Board() = default;
    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() { return uuid_; }
    virtual Backlight* GetBacklight() { return nullptr; }
    virtual Led* GetLed();
    virtual AudioCodec* GetAudioCodec() = 0;
    virtual bool GetTemperature(float& esp32temp);
    virtual Display* GetDisplay();
    virtual NetworkInterface* GetNetwork() = 0;
    virtual void StartNetwork() = 0;
    virtual const char* GetNetworkStateIcon() = 0;
    virtual std::string GetSystemInfoJson();
    virtual void SetPowerSaveMode(bool enabled) = 0;
    virtual std::string GetBoardJson() = 0;
    virtual std::string GetDeviceStatusJson() = 0;
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif // BOARD_H
