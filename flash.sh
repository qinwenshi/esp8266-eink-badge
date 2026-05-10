#!/usr/bin/env bash
# flash.sh — 编译并烧录 ESP8266 墨水屏固件
#
# 用法:
#   ./flash.sh              # 烧录 epd-sync 固件（默认）
#   ./flash.sh -p /dev/cu.usbserial-XXXX  # 指定串口
#   ./flash.sh monitor      # 仅打开串口监视器（不烧录）

set -e

FQBN="esp8266:esp8266:nodemcuv2"
BAUD=115200
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# ── 解析参数 ──────────────────────────────────────────────
TARGET="epd-sync"
PORT=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        monitor)   TARGET="monitor";  shift ;;
        -p|--port) PORT="$2";         shift 2 ;;
        *)         echo "未知参数: $1"; exit 1 ;;
    esac
done

# ── 自动探测串口 ───────────────────────────────────────────
if [[ -z "$PORT" ]]; then
    PORT=$(ls /dev/cu.usbserial-* /dev/ttyUSB* /dev/ttyACM* 2>/dev/null | head -1)
    if [[ -z "$PORT" ]]; then
        echo "❌ 未找到串口设备，请用 -p 手动指定"
        exit 1
    fi
    echo "🔍 自动探测串口: $PORT"
fi

# ── 检查 arduino-cli ──────────────────────────────────────
if ! command -v arduino-cli &>/dev/null; then
    echo "❌ arduino-cli 未安装，请先运行:"
    echo "   brew install arduino-cli"
    exit 1
fi

# ── monitor 模式 ──────────────────────────────────────────
if [[ "$TARGET" == "monitor" ]]; then
    echo "📡 串口监视器 $PORT @ ${BAUD} baud（Ctrl-C 退出）"
    arduino-cli monitor -p "$PORT" --config "baudrate=$BAUD"
    exit 0
fi

SKETCH="$SCRIPT_DIR/epd-sync"

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  固件 : EPD Sync"
echo "  串口 : $PORT"
echo "  板型 : $FQBN"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# ── 编译 ─────────────────────────────────────────────────
echo ""
echo "🔨 编译中..."
arduino-cli compile --fqbn "$FQBN" "$SKETCH"

# ── 烧录 ─────────────────────────────────────────────────
echo ""
echo "⚡ 烧录中..."
arduino-cli upload --fqbn "$FQBN" -p "$PORT" "$SKETCH"

echo ""
echo "✅ 烧录完成！"
echo ""
echo "💡 查看串口输出:"
echo "   ./flash.sh monitor -p $PORT"
