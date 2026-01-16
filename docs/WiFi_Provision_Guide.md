# WiFi SoftAP 配网集成指南

## 📋 快速开始

### 1. 添加组件依赖

编辑 `/home/lm/esp_project/modern_light/Lamp-ESP32-S3/main/idf_component.yml`，在 `dependencies` 部分添加：

```yaml
dependencies:
  78/esp-wifi-connect: "~3.0.2"
```

### 2. 更新 CMakeLists.txt

编辑 `/home/lm/esp_project/modern_light/Lamp-ESP32-S3/main/CMakeLists.txt`，添加新创建的文件到源文件列表：

```cmake
idf_component_register(
    SRCS 
        # ... 现有的文件 ...
        "network/wifi_provision_manager.cc"
    
    INCLUDE_DIRS 
        "."
        # ... 其他包含目录 ...
)
```

### 3. 在主程序中使用

参考 `examples/wifi_provision_example.cc` 中的示例代码。基本用法：

```cpp
#include "network/wifi_provision_manager.h"

// 初始化
auto& wifi_mgr = WiFiProvisionManager::GetInstance();
wifi_mgr.Initialize("Lamp", "zh-CN");  // 设备名称, 语言

// 设置回调
wifi_mgr.SetOnConnectedCallback([](const std::string& ssid) {
    printf("WiFi 已连接: %s\n", ssid.c_str());
});

// 启动（自动判断是否需要配网）
wifi_mgr.Start();
```

## 🔧 配网流程

1. **首次使用（无 WiFi 配置）**：
   - ESP32-S3 自动创建热点：`Lamp-XXXX`
   - 用户手机连接到热点
   - 浏览器自动弹出配置页面（强制门户）
   - 选择 WiFi 并输入密码
   - 设备保存配置并重启

2. **后续使用（有 WiFi 配置）**：
   - 自动连接到已保存的 WiFi
   - 如果连接失败，可手动进入配网模式

## 📱 Web 配置界面功能

- WiFi 扫描和列表显示
- WiFi 连接测试
- 多个 WiFi 配置管理
- 高级设置（发射功率、省电模式等）

## 🔨 编译和烧录

```bash
cd /home/lm/esp_project/modern_light/Lamp-ESP32-S3

# 清理并重新配置（首次添加组件依赖后需要）
idf.py reconfigure

# 编译
idf.py build

# 烧录
idf.py flash monitor
```

## 📚 API 参考

详见：
- `main/network/wifi_provision_manager.h` - API 接口定义
- `examples/wifi_provision_example.cc` - 完整使用示例
- `implementation_plan.md` - 详细的实施计划

## ⚠️ 注意事项

1. 必须先初始化 NVS Flash
2. 必须先创建默认事件循环
3. 配网时会占用端口 80（HTTP）和 53（DNS）
4. Web 服务器需要足够的堆内存

## 🎯 下一步

1. 添加组件依赖
2. 编译测试
3. 烧录到设备
4. 使用手机测试配网流程
