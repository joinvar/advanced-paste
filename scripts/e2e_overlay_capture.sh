#!/usr/bin/env bash
# ====================================================================
# scripts/e2e_overlay_capture.sh
# ====================================================================
# 本地端到端流程测试（bash 版本），按下列固定剧本跑一遍多屏交互，
# 每步只在"当下关注的那块屏"截一张，最后共 5 张 PNG。
#
# 剧本：
#   步骤 1 初始状态        对每块屏各截一张（基准对照）
#   步骤 2 光标到第 1 屏 + Ctrl+Alt+X，在第 1 屏截一张
#                          （overlay 在快捷键触发时光标所在屏出现）
#   步骤 3 光标到第 2 屏    在第 2 屏截一张（观察第 2 屏在 overlay
#                          存在期间看起来什么样）
#   步骤 4 在第 2 屏按下左键并拖动开始框选，截一张
#   步骤 5 按 ESC 取消 overlay（不截图）
#          注意：右键取消在多屏场景可能失效，所以统一用 ESC
#
# 前置：已在 build/ 下执行过 cmake --build . --config Release
# 限制：必须本机跑（真实桌面会话），headless 环境接收不到 SendInput
# 干扰：运行期间 overlay 会短暂显示约 2 秒，期间请勿动键鼠
#
# 用法：
#   scripts/e2e_overlay_capture.sh             # 默认输出目录
#   scripts/e2e_overlay_capture.sh <out-dir>   # 自定义目录
# ====================================================================

set -euo pipefail  # 严格模式：命令失败退出、未定义变量报错、管道失败报错

# ── 路径定位：脚本所在目录的上一级作为项目根 ───────────────
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

GUI="$ROOT/build/Release/AdvancedPaste.exe"      # GUI 主程序
CLI="$ROOT/build/Release/AdvancedPasteCli.exe"   # 命令行工具
OUT="${1:-$ROOT/build/Release/e2e_out}"          # 输出目录，默认项目内

# ── 坐标参数 ──────────────────────────────────────────────────
# 传给 send-mouse 的 X,Y 在指定 --monitor N 后是该屏的局部坐标
# （左上角为 0,0）。用 500,500 作为"屏中心偏上"的点，离边缘足够远，
# 避免贴边误差；任何屏分辨率都不会越界
MON_CENTER_X=500
MON_CENTER_Y=500
# 步骤 4 里把鼠标按下后再移动到 (1200,800) 制造拖拽
MON_DRAG_X=1200
MON_DRAG_Y=800

# ── 1. 检查构建产物是否齐全 ─────────────────────────────────
if [ ! -f "$GUI" ] || [ ! -f "$CLI" ]; then
    echo "ERR: 找不到构建产物：" >&2
    echo "  $GUI" >&2
    echo "  $CLI" >&2
    echo "请先在 build/ 目录下执行：cmake --build . --config Release" >&2
    exit 1
fi

# ── 2. 清空并重建输出目录 ────────────────────────────────────
# 目的：保证目录里只有本次跑出来的 PNG，不与旧帧混杂
rm -rf "$OUT"
mkdir -p "$OUT"

# ── 3. 注册退出清理 ──────────────────────────────────────────
# 退出时做两件事：
#   1）兜底抬起鼠标（防止脚本中途失败时左键按着未释放）
#   2）强制关掉 GUI 主程序（保证 overlay 不残留）
# 为什么结尾要 taskkill GUI？因为 ESC 关 overlay 不 100% 可靠——
# SendInput 的 down/up 事件可能让前台焦点跳到其它窗口，ESC 就被别
# 的窗口接收而不是 overlay。为避免 overlay 残留污染后续测试，
# 索性每次跑完都让 GUI 彻底退出。用户日常使用时再手动启动即可。
cleanup() {
    "$CLI" send-mouse up 2>/dev/null || true
    taskkill //IM AdvancedPaste.exe //F >/dev/null 2>&1 || true
}
trap cleanup EXIT

# ── 4. 从干净状态启动 GUI ────────────────────────────────────
# 先把可能残留的旧 GUI 进程杀掉，再启动一份新的，保证 overlay 状态
# 和快捷键注册从零开始；否则上次跑完如果有 overlay 残留，本次
# StartCapture 会检查 g_hOverlay != NULL 而直接 return，新的 overlay
# 根本不会出现
taskkill //IM AdvancedPaste.exe //F >/dev/null 2>&1 || true
sleep 0.3

echo "[启动] GUI 主程序（干净状态）..."
GUI_WIN="$(cygpath -w "$GUI" 2>/dev/null || echo "$GUI")"
powershell -Command "Start-Process -FilePath '$GUI_WIN'" > /dev/null
sleep 1.5   # 等 GUI 初始化并注册全局快捷键

# ════════════════════════════════════════════════════════════════
# 步骤 1：初始状态，对每块屏各截一张（作为基准对照）
# ────────────────────────────────────────────────────────────────
# 硬编码两块屏（monitor 0 和 monitor 1）。若你机器屏数不同，改下面
# 这两行（或未来把 list-monitors 的 JSON 解析进来动态枚举）
# ════════════════════════════════════════════════════════════════
echo ""
echo "=== 步骤 1/5：初始状态，每屏各截一张 ==="
"$CLI" capture --monitor 0 --out "$OUT/01_init_mon0.png" > /dev/null
"$CLI" capture --monitor 1 --out "$OUT/01_init_mon1.png" > /dev/null
echo "  -> 01_init_mon0.png  01_init_mon1.png"

# ════════════════════════════════════════════════════════════════
# 步骤 2：光标 → 第 1 屏 → Ctrl+Alt+X，然后截第 1 屏
# ────────────────────────────────────────────────────────────────
# overlay 在 StartCapture 时根据当前光标所在屏创建，一旦创建就固定，
# 不跟随光标切屏。所以这里先把光标明确定位到第 1 屏再发快捷键，
# 保证 overlay 出现在第 1 屏。
# ════════════════════════════════════════════════════════════════
echo ""
echo "=== 步骤 2/5：光标 -> 第 1 屏，发 Ctrl+Alt+X，截第 1 屏 ==="
"$CLI" send-mouse move "$MON_CENTER_X,$MON_CENTER_Y" --monitor 0
sleep 0.3
"$CLI" send-keys "ctrl+alt+x"
sleep 0.5  # 等 overlay 渲染第一帧
"$CLI" capture --monitor 0 --out "$OUT/02_hotkey_mon0.png" > /dev/null
echo "  -> 02_hotkey_mon0.png"

# ════════════════════════════════════════════════════════════════
# 步骤 3：光标 → 第 2 屏，截第 2 屏
# ────────────────────────────────────────────────────────────────
# 光标可以自由跨屏移动（WM_LBUTTONDOWN 之前没有 SetCapture）。
# 这一步关注的是：overlay 在第 1 屏时，第 2 屏看起来是什么样？
# 预期答案：第 2 屏仍是普通桌面（overlay 不跨屏）
# ════════════════════════════════════════════════════════════════
echo ""
echo "=== 步骤 3/5：光标 -> 第 2 屏，截第 2 屏 ==="
"$CLI" send-mouse move "$MON_CENTER_X,$MON_CENTER_Y" --monitor 1
sleep 0.5  # 等鼠标到位 + 画面稳定
"$CLI" capture --monitor 1 --out "$OUT/03_move_mon1.png" > /dev/null
echo "  -> 03_move_mon1.png"

# ════════════════════════════════════════════════════════════════
# 步骤 4：在第 2 屏按下左键并拖动模拟框选，截第 2 屏
# ────────────────────────────────────────────────────────────────
# 按下后不抬起，拖到 (1200,800) 再截图。当前 overlay 在第 1 屏，
# 鼠标事件可能被 overlay 接收也可能没（SetCapture 仅在 overlay 内
# 按下时触发），这一步主要是观察第 2 屏在拖拽过程的画面
# ════════════════════════════════════════════════════════════════
echo ""
echo "=== 步骤 4/5：在第 2 屏按下并拖动，截第 2 屏 ==="
"$CLI" send-mouse down
sleep 0.1
"$CLI" send-mouse move "$MON_DRAG_X,$MON_DRAG_Y" --monitor 1
sleep 0.3
"$CLI" capture --monitor 1 --out "$OUT/04_dragging_mon1.png" > /dev/null
echo "  -> 04_dragging_mon1.png"

# ════════════════════════════════════════════════════════════════
# 步骤 5：ESC 取消（不截图）
# ────────────────────────────────────────────────────────────────
# 关键细节：ESC 关 overlay 前**必须先把鼠标移回 overlay 所在屏**
# （即按下快捷键的第 1 屏）。否则鼠标留在第 2 屏，某些场景下 ESC
# 的键盘事件可能不被 overlay 接收，导致 overlay 关不掉。
# 注意：右键取消在多屏场景可能失效（已知问题），所以固定用 ESC。
# overlay 在 WM_KEYDOWN 里拦 ESC 调 DestroyWindow，不管是否在拖拽
# 都能关；先抬起鼠标再移动，避免"按着拖"的状态跨屏引发副作用
# ════════════════════════════════════════════════════════════════
echo ""
echo "=== 步骤 5/5：ESC 取消 overlay ==="
"$CLI" send-mouse up                                          # 先松开（之前 down 过）
sleep 0.1
"$CLI" send-mouse move "$MON_CENTER_X,$MON_CENTER_Y" --monitor 0  # 鼠标回第 1 屏
sleep 0.2
"$CLI" send-keys "esc"                                        # 发 ESC 关 overlay
sleep 0.3

# ════════════════════════════════════════════════════════════════
# 断言与汇报：5 张 PNG 都到齐
# ════════════════════════════════════════════════════════════════
COUNT=$(find "$OUT" -maxdepth 1 -name "*.png" | wc -l)
echo ""
echo "----------------------------------------"
echo "产出 PNG: $COUNT / 5"
ls "$OUT" | sed 's/^/  /'

if [ "$COUNT" -ne 5 ]; then
    echo "FAIL: 预期 5 张, 实际 $COUNT" >&2
    exit 1
fi

echo ""
echo "PASS: 文件数量符合预期"
echo "帧目录: $OUT"
echo ""
echo "推荐查看顺序："
echo "  01_init_mon0.png / 01_init_mon1.png  -- 原始桌面（两屏基准）"
echo "  02_hotkey_mon0.png                    -- 快捷键触发后的第 1 屏（应有 overlay）"
echo "  03_move_mon1.png                      -- 光标移到第 2 屏后的第 2 屏"
echo "  04_dragging_mon1.png                  -- 框选过程中的第 2 屏"
