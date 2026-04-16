#!/usr/bin/env bash
# scripts/e2e_overlay_capture.sh
# 本地端到端流程测试：启动 GUI → 后台连拍 → 模拟快捷键触发 overlay
# → 模拟鼠标拖拽框选 → ESC 取消。产物是一串 PNG，可人工或脚本比对
# 首帧/中间帧尺寸差异确认 overlay 的确被触发。
#
# 前置：在 build/ 下已跑过 cmake --build . --config Release
# 限制：必须在有真实桌面会话的本机跑；headless CI runner 无法接收 SendInput。
# 干扰：运行期间会有 overlay 在屏幕上短暂显示（~3 秒），过程别动鼠标键盘。
#
# 用法：
#   scripts/e2e_overlay_capture.sh            # 默认输出到 build/Release/e2e_out
#   scripts/e2e_overlay_capture.sh <out-dir>  # 自定义输出目录

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GUI="$ROOT/build/Release/AdvancedPaste.exe"
CLI="$ROOT/build/Release/AdvancedPasteCli.exe"
OUT="${1:-$ROOT/build/Release/e2e_out}"

LOOP_COUNT=15
LOOP_INTERVAL=250  # ms

# ── 1. 检查构建产物 ────────────────────────────────────────
if [ ! -f "$GUI" ] || [ ! -f "$CLI" ]; then
    echo "ERR: 找不到构建产物：" >&2
    echo "  $GUI" >&2
    echo "  $CLI" >&2
    echo "请先在 build/ 目录下执行：cmake --build . --config Release" >&2
    exit 1
fi

# ── 2. 清理 + 建立输出目录 ─────────────────────────────────
rm -rf "$OUT"
mkdir -p "$OUT"

# ── 3. 清理函数：脚本异常退出时兜底杀后台连拍 ─────────────
cleanup() {
    if [ -n "${LOOP_PID:-}" ] && kill -0 "$LOOP_PID" 2>/dev/null; then
        kill "$LOOP_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT

# ── 4. 确保 GUI 主程序在跑 ─────────────────────────────────
if ! tasklist //FI "IMAGENAME eq AdvancedPaste.exe" 2>/dev/null \
        | grep -qi "AdvancedPaste.exe"; then
    echo "[启动] GUI 主程序..."
    # PowerShell 需要 Windows 风格路径，MSYS 下用 cygpath 转
    GUI_WIN="$(cygpath -w "$GUI" 2>/dev/null || echo "$GUI")"
    powershell -Command "Start-Process -FilePath '$GUI_WIN'" > /dev/null
    sleep 1.5
else
    echo "[复用] 已运行中的 GUI 主程序"
fi

# ── 5. 后台连拍 ────────────────────────────────────────────
echo "[连拍] $LOOP_COUNT 帧 × ${LOOP_INTERVAL}ms → $OUT"
"$CLI" capture --monitor 1 --loop "$LOOP_COUNT" --interval "$LOOP_INTERVAL" \
               --out-dir "$OUT" --out-prefix f_ &
LOOP_PID=$!

# ── 6. 注入键鼠，模拟用户完整流程 ──────────────────────────
sleep 0.3
echo "[注入] Ctrl+Alt+X （触发 overlay）"
"$CLI" send-keys "ctrl+alt+x"

sleep 0.6
echo "[注入] 鼠标拖拽 (400,300) → (1300,800) 【主屏局部坐标】"
"$CLI" send-mouse drag 400,300 1300,800 --monitor 1

sleep 0.8
echo "[注入] ESC （取消 overlay）"
"$CLI" send-keys "esc"

# ── 7. 等连拍结束 ──────────────────────────────────────────
wait "$LOOP_PID"
LOOP_PID=""  # 防止 cleanup 再次尝试 kill

# ── 8. 基本断言 ────────────────────────────────────────────
COUNT=$(find "$OUT" -maxdepth 1 -name "f_*.png" | wc -l)
echo "---"
echo "产出 PNG: $COUNT / $LOOP_COUNT"

if [ "$COUNT" -ne "$LOOP_COUNT" ]; then
    echo "FAIL: 预期 $LOOP_COUNT 张 PNG，实际 $COUNT" >&2
    exit 1
fi

SIZE_FIRST=$(stat -c "%s" "$OUT/f_000.png")
SIZE_MID=$(stat -c "%s" "$OUT/f_005.png")

if [ "$SIZE_FIRST" -eq "$SIZE_MID" ]; then
    echo "FAIL: 首帧和中间帧大小都是 $SIZE_FIRST 字节，overlay 可能没触发" >&2
    echo "      检查：GUI 进程是否在跑、Ctrl+Alt+X 是否被其他程序抢占、屏幕是否被锁定" >&2
    exit 2
fi

echo "PASS: f_000=${SIZE_FIRST}B, f_005=${SIZE_MID}B，画面确有变化"
echo "帧目录: $OUT"
echo "提示:   打开 f_005.png 可看到 overlay 的 ST_EDIT 态（选区 + 橙色边框 + 工具栏）"
