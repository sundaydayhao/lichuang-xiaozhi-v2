# 立创小智 v2

这是一个面向立创 `ESP32-S3` 硬件的语音 AI 固件项目，基于 `ESP-IDF` 和 `esp_xiaozhi` 组件实现语音对话、触摸界面、摄像头能力、MCP 工具调用，以及一组围绕屏幕和相机展开的本地交互场景。

这份 README 只描述当前仓库的真实状态，不再沿用上游多板通用版说明。当前代码在构建系统中固定使用 `main/boards/lichuang-s3`，默认目标芯片为 `esp32s3`。

## 项目定位

- 固定板型：`lichuang-s3`
- 固定目标：`ESP32-S3`
- 默认配置：`16MB Flash + PSRAM`
- 默认分区表：`partitions/v2/16m.csv`
- 默认协议栈：由 `esp_xiaozhi` 组件负责聊天连接，按服务端配置使用 WebSocket 或 MQTT

如果你要做的是自定义板卡移植，而不是直接编译这块板子的固件，请先看 [main/boards/README.md](main/boards/README.md)。

## 硬件基线

从当前板级代码可以确认，这个分支围绕以下硬件资源开发：

- `ESP32-S3`
- `320 x 240` ST7789 SPI LCD
- FT5X06 触摸控制器
- ES8311 扬声器输出 codec
- ES7210 麦克风输入 codec
- 板载摄像头
- 板载 LED
- BOOT 按键
- `assets` 与 `storage` 两个 SPIFFS 分区

其中：

- `assets` 分区用于字体、表情、音效、语言文件、唤醒词模型等资源
- `storage` 分区用于运行时存储，例如拍照后的本地图片文件

## 已实现功能

### 语音与系统能力

- Wi-Fi 联网与配网 AP
- 离线唤醒词检测，默认启用 AFE 唤醒
- 可选自定义唤醒词
- Opus 音频编码
- 流式 ASR / LLM / TTS 语音对话
- 设备启动后自动进行 OTA 检查、激活检查和资源包检查
- `assets` 在线热更新
- 屏幕状态栏、触摸菜单、亮度控制、主题切换
- 本地相册与 `/storage` 持久化存储
- BLE HID 能力，供手机遥控场景使用

### MCP 工具能力

设备侧 MCP 已注册的能力包括：

- 获取设备状态
- 调节音量
- 调节屏幕亮度
- 切换屏幕主题
- 拍照并请求图像解释
- 远程自拍杆动作控制
- AI 闹钟设置、取消、状态查询

### 屏幕内置场景

主界面菜单中已经实现或接入了以下场景：

- AI 闹钟
- 百科问答
- 翻译官
- 拍识万物
- AI 自拍杆
- AI 打印
- 单词听写
- 看相
- 魔镜美妆
- 助眠音乐
- 设置

需要注意：

- `拍识万物`、`看相`、`魔镜美妆` 这类视觉能力依赖摄像头和服务端模型能力
- `AI 打印` 当前代码中调用的是在线生图接口，使用时必须联网
- `AI 自拍杆` 依赖 BLE HID，需要先和手机配对

## 目录说明

- `main/`：主应用代码
- `main/boards/lichuang-s3/`：当前固定板型的板级实现
- `main/display/`：LCD、LVGL UI、各个功能菜单
- `main/audio/`：音频采集、处理、唤醒、编解码
- `main/protocols/`：与 `esp_xiaozhi` 的协议对接
- `components/ble_hid/`：BLE HID 实现，供 AI 自拍杆功能使用
- `partitions/v2/`：分区表
- `scripts/`：打包、资源生成、音频调试等脚本

## 开发环境

建议环境：

- `ESP-IDF 5.5.0` 或更高版本
- Python 3
- CMake / Ninja
- 一根可烧录的 USB 数据线
- Linux 开发环境优先，构建和驱动体验通常更稳定

版本要求不要写低。当前 `main/idf_component.yml` 已经声明 `idf >= 5.5.0`，不建议再按老版本 README 用 `5.4` 去尝试。

## 编译前说明

这个项目第一次编译时会通过 IDF Component Manager 拉取依赖，并根据 `sdkconfig` 自动生成默认 `assets.bin`。因此：

- 首次构建时间会比普通 IDF 项目长
- 默认会一起准备 `assets` 分区内容
- 如果你不想烧录默认资源，可以在 `menuconfig` 里关闭

默认 `sdkconfig.defaults` 使用：

- `16MB Flash`
- `QIO`
- `Octal PSRAM`
- `partitions/v2/16m.csv`

如果你的硬件不是这个规格，需要你自己修改分区和相关配置，否则不要盲目烧录。

## 编译步骤

先进入已经导出好 `ESP-IDF` 环境的 shell，再执行：

```bash
cd lichuang-xiaozhi-v2
idf.py set-target esp32s3
idf.py build
```

如果你还没有导出 IDF 环境，先执行你本机对应的 `export` 脚本，例如：

```bash
. $IDF_PATH/export.sh
```

### 可选：先进入 menuconfig

建议第一次编译前先看一遍：

```bash
idf.py menuconfig
```

重点配置项都在 `Xiaozhi Assistant` 菜单下，包括：

- `Default OTA URL`
- `Flash Assets`
- `Custom Assets File`
- `Default Language`
- `Enable Wake Word Detection (AFE)`
- `Enable Custom Wake Word Detection`
- `Enable Audio Noise Reduction`
- `Enable Server-Side AEC`
- `Enable Acoustic WiFi Provisioning`
- `Enable Audio Debugger`
- `Enable Custom Message Reception`

其中最常用的几个点：

- 想接自建服务，先改 `Default OTA URL`
- 想换语言，改 `Default Language`
- 想关闭默认资源烧录，改 `Flash Assets`
- 想做自定义唤醒词，先关闭 AFE，再启用 `Custom Wake Word`

## 烧录与串口监视

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

退出串口监视：

```text
Ctrl + ]
```

如果你怀疑 NVS 里残留了旧配置，建议直接整片擦除后重烧：

```bash
idf.py -p /dev/ttyUSB0 erase-flash flash monitor
```

## 首次启动与使用方法

### 1. 首次上电

设备启动后会依次完成：

1. 初始化屏幕、音频、摄像头相关外设
2. 连接 Wi-Fi
3. 检查资源包更新
4. 检查固件更新
5. 按服务器要求执行激活流程
6. 启动聊天协议，进入待机

### 2. Wi-Fi 配网

如果设备里还没有保存 Wi-Fi，程序会自动进入配网模式，启动一个热点，热点名前缀是：

```text
Xiaozhi
```

屏幕会显示热点名和配网页地址。此时：

1. 手机或电脑连接这个热点
2. 用浏览器打开屏幕上显示的地址
3. 提交目标 Wi-Fi 的账号和密码
4. 设备重启或重新连网后进入正常模式

如果你在 `menuconfig` 中启用了 `Enable Acoustic WiFi Provisioning`，还可以使用声波配网。

### 3. 激活与在线配置

当前固件会通过 OTA 地址获取：

- 固件更新信息
- 激活码 / 激活挑战
- 聊天服务配置
- 服务器时间

默认 OTA 地址来自：

```text
https://api.tenclass.net/xiaozhi/ota/
```

如果你接的是自建服务，最稳妥的方式是在编译前就改掉这个配置。

### 4. 语音交互

正常待机后有两种常用入口：

- 说唤醒词
- 单击 BOOT 按键

当前应用逻辑里，单击 BOOT 的行为大致如下：

- 待机时：开始一次会话
- 播报中：打断当前说话
- 监听中：关闭当前音频会话
- 配网界面：切换到音频测试，再次单击回到配网界面

### 5. 触摸界面

主界面是触摸菜单，点击图标即可进入对应场景。

几个和使用相关的细节：

- `拍识万物`：进入后会打开摄像头预览，点击 AI 按钮发起识别；在该页面向左滑动可进入相册
- `AI 自拍杆`：进入后设备会启动 BLE HID，手机侧需要配对名为 `Xiaozhi-HID` 的蓝牙设备
- `AI 打印`：进入后直接说出你想画的内容，设备会把语音识别文本提交给在线生图接口
- `设置`：可切换语言、查看固件版本、MAC、IP、UUID 等信息；如果你预先写入了备选 OTA 地址，也可在这里切换服务器

## 资源与分区

当前默认分区表是 [partitions/v2/16m.csv](partitions/v2/16m.csv)，主要分区如下：

- `ota_0`：应用分区 A
- `ota_1`：应用分区 B
- `assets`：4MB，存放字体、表情、声音、模型等资源
- `storage`：4MB，存放运行期数据，如拍照文件

如果你想了解 `v2` 分区为什么这么设计，可以看 [partitions/v2/README.md](partitions/v2/README.md)。

## 发行打包

如果你要导出合并固件和发布压缩包，可以用仓库里的脚本：

```bash
python scripts/release.py lichuang-s3
```

或者对当前已经编译好的配置直接打包：

```bash
python scripts/release.py
```

输出目录：

```text
releases/
```

脚本会调用：

- `idf.py build`
- `idf.py merge-bin`

并生成：

- `v<version>_lichuang-s3.bin`
- `v<version>_lichuang-s3_merged.bin`
- 对应 zip 包

## 常用调试入口

- 音频调试 UDP：`Enable Audio Debugger`
- 资源打包脚本：[scripts/build_default_assets.py](scripts/build_default_assets.py)
- 自定义 SPIFFS 资源说明：[scripts/spiffs_assets/README.md](scripts/spiffs_assets/README.md)
- BLE HID 组件说明：[components/ble_hid/README.md](components/ble_hid/README.md)

## 相关文档

- [main/boards/README.md](main/boards/README.md) - 自定义开发板指南
- [docs/mcp-usage.md](docs/mcp-usage.md) - MCP 用法说明
- [docs/mcp-protocol.md](docs/mcp-protocol.md) - MCP 交互流程
- [docs/websocket.md](docs/websocket.md) - WebSocket 协议说明
- [docs/mqtt-udp.md](docs/mqtt-udp.md) - MQTT + UDP 说明

## 注意事项

- 这个仓库当前不是“支持几十块板”的通用固件，而是固定面向 `lichuang-s3`
- 构建要求以仓库里的 `idf_component.yml` 和 `sdkconfig.defaults` 为准，不要再按旧 README 的版本要求执行
- `AI 打印`、视觉解释、云端对话等能力都依赖网络和服务端接口，不是纯离线功能
- 若硬件参数和本仓库默认配置不一致，先改板级配置和分区，再谈烧录

## License

本仓库使用 [LICENSE](LICENSE) 中的许可证。
