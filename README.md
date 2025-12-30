# xiaozhi-esp32 固件说明

本项目是基于 ESP-IDF 的 ESP32 语音交互固件，集成 MCP 协议、语音编解码、屏幕显示、电源管理等能力，面向小智 AI 聊天机器人硬件设备。代码以 MIT 许可证发布，可自由使用与定制。

## 目录结构

```
├─main/                 # 业务入口与组件桥接（MCP、音频、OTA、系统信息）
│  ├─application.cc     # 应用初始化与主事件循环
│  ├─mcp_server.*       # 设备端 MCP 服务
│  ├─assets.*           # 资源注册（音频、UI 等）
│  ├─ota.*              # 空中升级流程
│  ├─settings.*         # 配置管理
│  ├─system_info.*      # 设备与运行状态上报
│  └─boards/            # 板级适配与引脚定义
├─managed_components/   # 通过 component manager 拉取的依赖（音频、LCD、按钮等）
├─partitions/           # 分区表配置（v1/v2 等）
├─scripts/              # 构建、烧录、工具脚本
├─CMakeLists.txt        # 顶层构建入口
└─sdkconfig*            # SDK 配置及默认模板
```

## 功能概览

- 语音交互：I2S + ES8388 音频链路，Opus 编解码，支持麦克风采集与扬声器播放
- 协议接入：内置 MCP，支持 WebSocket/MQTT 等下行指令与上行事件
- 显示与输入：LCD/OLED 表情显示，按键与触控输入组件（取决于具体板型）
- 联网能力：Wi-Fi 及选配 4G (ML307) 模块接入
- OTA 升级：固件空中更新与回滚保护
- 电源与状态：电池电量估算、系统信息上报、基础日志与调试

## 快速开始

1. 安装 ESP-IDF 5.4+ 开发环境，并将 `idf.py` 加入 PATH。
2. 选择目标板的默认配置（示例：`sdkconfig.defaults.esp32s3`），执行：
   ```bash
   idf.py set-target esp32s3 && idf.py build flash monitor
   ```
3. 烧录完成后，设备将连接预设服务器并启动语音与 MCP 服务，可通过控制台观察日志。

## 许可证

MIT
