# esp8266-eink

ESP8266 + WFT0371CZ78 3.7 寸黑白红三色墨水屏项目。包含两个独立固件和一套 Python CLI 工具。

## 硬件

| 参数 | 值 |
|------|----|
| 主控 | ESP8266 (NodeMCU v2) |
| 屏幕 | WFT0371CZ78 / GDEY037Z03 |
| 控制芯片 | UC8253 |
| 分辨率 | 240 × 416 |
| 颜色 | 黑 / 白 / 红 三色 |
| 局部刷新 | 支持（仅黑白通道） |

**接线（ESP8266 → 屏幕）：**

| ESP8266 | 功能 |
|---------|------|
| GPIO15 (D8) | CS   |
| GPIO4  (D2) | DC   |
| GPIO2  (D4) | RST  |
| GPIO5  (D1) | BUSY |
| GPIO13 (D7) | MOSI |
| GPIO14 (D5) | SCK  |

---

## 固件一：Anki 卡片复习器（`esp8266-eink.ino`）

通过 AnkiConnect 从局域网 Anki 实例拉取卡片，在墨水屏上显示并用按钮操作。

### 配置（`config.h`）

```c
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
#define ANKI_HOST     "192.168.1.x"   // 运行 Anki 的电脑 IP
#define ANKI_PORT     8765
#define ANKI_QUERY    "is:new"        // AnkiConnect 查询语句
```

### 按键操作

| 状态 | 短按 | 长按 |
|------|------|------|
| 正面 | 翻到背面 | 跳过 |
| 背面 | Good（ease=3） | Again（ease=1） |

### 依赖库

- GxEPD2
- ArduinoJson
- Adafruit GFX

### 编译 & 烧录

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 .
arduino-cli upload  --fqbn esp8266:esp8266:nodemcuv2 -p /dev/cu.usbserial-XXXX .
```

---

## 固件二：EPD 同步显示（`epd-sync/epd-sync.ino`）

接收来自上位机的图片并刷新屏幕，支持 Pull（设备轮询）和 Push（上位机主动推送）两种模式。

### 配置（`config.h`）

```c
#define SYNC_MODE         0           // 0=Pull  1=Push
#define SYNC_HOST         "192.168.1.x"  // epd-tool serve 所在机器的 IP（Pull 模式）
#define SYNC_PORT         8080
#define PULL_INTERVAL_MS  60000       // 轮询间隔，毫秒（Pull 模式）
```

### Pull 模式（`SYNC_MODE 0`）

设备每隔 `PULL_INTERVAL_MS` 向上位机请求：
1. `GET /version` — 获取当前版本号
2. 版本变化时 `GET /current.epd` — 流式下载图像并刷新

### Push 模式（`SYNC_MODE 1`）

设备运行 HTTP Server，上位机主动推送：

| 接口 | 方法 | 说明 |
|------|------|------|
| `/push`   | POST | 接收 `.epd` 原始字节，刷新屏幕 |
| `/status` | GET  | 返回 `{"version": N, "rssi": N}` |

设备通过 mDNS 广播 `epd-display.local`，无需手动填 IP。

### 编译 & 烧录

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 epd-sync/
arduino-cli upload  --fqbn esp8266:esp8266:nodemcuv2 -p /dev/cu.usbserial-XXXX epd-sync/
```

---

## CLI 工具（`cli/`）

Python 工具，将 YAML/JSON 模板渲染成 `.epd` 图像并同步到屏幕。

### 安装

```bash
cd cli
pip install -e .
```

### 命令

#### `epd render` — 本地渲染预览

```bash
epd render templates/badge.yaml \
    -d '{"name":"张三","title":"工程师","department":"研发部","company":"ACME"}' \
    --preview          # 打开预览窗口
    --out output.epd   # 保存为 .epd 文件
```

#### `epd serve` — 启动服务（Pull 模式）

```bash
epd serve templates/meeting-room.yaml \
    -d '{"room_name":"会议室A","status":"Available","next_meeting":"Sales","next_time":"14:00"}' \
    --port 8080 \
    --watch            # 模板文件变化时自动重新渲染
```

#### `epd push` — 直接推送（Push 模式）

```bash
# 推送模板（实时渲染后发送）
epd push templates/namecard.yaml \
    -d '{"name":"李四","title":"设计师","email":"li@acme.com","phone":"138-0000-0000","company":"ACME"}' \
    -H epd-display.local

# 推送已有 .epd 文件
epd push output.epd -H 192.168.1.42 --port 80
```

### 内置模板

| 模板 | 说明 | 变量 |
|------|------|------|
| `templates/badge.yaml` | 工卡 | `name` `title` `department` `company` |
| `templates/namecard.yaml` | 名片 | `name` `title` `email` `phone` `company` |
| `templates/meeting-room.yaml` | 会议室状态 | `room_name` `status` `next_meeting` `next_time` |

### 模板格式

```yaml
size: [240, 416]
background: white
elements:
  - type: text
    text: "{{ name }}"
    font: Helvetica
    size: 28
    color: black        # black | red | white
    pos: [20, 100]
    align: left         # left | center

  - type: rect
    rect: [x, y, w, h]
    fill: red
    outline: black      # 可选

  - type: line
    start: [20, 200]
    end:   [220, 200]
    color: red
    width: 2

  - type: image
    src: "avatar.png"
    rect: [x, y, w, h]
```

---

## `.epd` 文件格式

| 偏移 | 大小 | 内容 |
|------|------|------|
| 0 | 4 B | Magic: `EPD2`（`0x45 0x50 0x44 0x32`） |
| 4 | 2 B | Width（uint16 LE）= 240 |
| 6 | 2 B | Height（uint16 LE）= 416 |
| 8 | 12480 B | BW 平面（1=白 0=黑），写入显存 `0x10` |
| 12488 | 12480 B | Red 平面（1=无红 0=红），写入显存 `0x13` |
| **合计** | **24968 B** | |

---

## 项目结构

```
esp8266-eink/
├── esp8266-eink.ino          # Anki 复习器固件
├── config.h                  # WiFi / Anki / EPD Sync 配置
├── GxEPD2_370C_UC8253.h/.cpp # UC8253 屏幕驱动
├── epd-sync/                 # EPD 同步固件
│   ├── epd-sync.ino
│   ├── GxEPD2_370C_UC8253.h/.cpp
│   └── config.h
└── cli/                      # Python CLI 工具
    ├── pyproject.toml
    ├── README.md
    ├── templates/
    │   ├── badge.yaml
    │   ├── namecard.yaml
    │   └── meeting-room.yaml
    └── epd_tool/
        ├── cli.py
        ├── render.py
        ├── serve.py
        ├── push.py
        └── format.py
```
