# Advanced Paste 截图工具

中文 | [English](README_EN.md)

## 快捷键

- **Ctrl+Alt+X**（默认，可配置） — 启动截图
- **ESC** — 取消截图
- **Ctrl+Z / 选区内右键** — 撤销最后一个标注
- **选区外右键** — 完成截图（等同点击「完成」）

## 使用流程

1. 运行 `AdvancedPaste.exe`，程序常驻系统托盘
2. 按 `Ctrl+Alt+X` 进入截图模式，光标所在屏冻结并变暗；多屏环境下，未开始框选前 overlay 会跟随光标跨屏迁移
3. **选择区域**：
   - 移动鼠标，自动高亮光标所在窗口，单击即可选中
   - 或按住左键拖拽，自由选择矩形区域
4. **标注编辑**（可选）：
   - 选区下方出现工具栏：「箭头」「矩形」「画笔」「完成」「取消」
   - 点击「箭头」「矩形」或「画笔」激活工具，在选区内拖拽绘制
   - 「画笔」支持自由手绘曲线，按住左键移动即可
   - 选区内右键或 Ctrl+Z 撤销上一个标注
   - 选区外右键直接完成截图
5. 点击「完成」（或选区外右键），截图（含标注）保存到内存，同时写入系统剪贴板
6. 在资源管理器的真实文件夹标签页中，按 `Ctrl+V` 粘贴为 PNG 文件，文件名根据 `config.ini` 中的 `Pattern` 配置自动生成
7. 粘贴完成后自动恢复正常的复制粘贴功能；截图也可在其他应用中通过 `Ctrl+V` 粘贴为图片

补充说明：

- 支持资源管理器多标签页；当前激活的文件夹标签页会作为保存目标
- `主页`、`快速访问`、`此电脑` 等非真实目录位置不属于文件保存目标

## 退出

右键系统托盘图标 → 「退出」

## 配置文件

程序首次运行时会在 exe 同目录下自动创建 `config.ini`：

```ini
[Settings]
Hotkey=Ctrl+Alt+X
Pattern=
AutoFinishOnSelect=0
SaveDir=
```

**Hotkey** — 截图快捷键，支持修饰键 `Ctrl`、`Alt`、`Shift`、`Win`，用 `+` 连接，最后一个为主键（A-Z、0-9、F1-F24）。修改后需重启程序生效。即使快捷键被系统占用（如 `Shift+Win+S`），程序也会通过键盘钩子优先拦截。

**Pattern** — 输出文件名前缀，可为空。支持占位符：
- `{Date}` — 当前日期，格式 `20260212`
- `{Time}` — 当前时间，格式 `143025`

**AutoFinishOnSelect** — 框选完成后是否立即结束截图：
- `0`（默认）— 框选后进入编辑态，需要点击「完成」或在选区外右键结束
- `1` — 左键抬起后立即完成截图并关闭 overlay，不进入编辑态

**SaveDir** — 自动保存目录，可为空。支持环境变量（如 `%USERPROFILE%\Pictures`），目录不存在时自动创建：
- _空_（默认）— 保持原流程：截图放入剪贴板，`Ctrl+V` 在资源管理器里粘贴为 PNG
- _有值_ — 截图完成后立刻按 `Pattern` 命名保存 PNG 到该目录，并把**文件绝对路径**写入剪贴板文本，直接在终端 `Ctrl+V` 即可粘出路径发给命令行工具；同时仍保留图像格式，聊天/图像类应用里 `Ctrl+V` 照常粘成图片

示例：

| Pattern | 生成文件名 |
|---|---|
| _(空)_ | `1.png`, `2.png` |
| `screenshot_` | `screenshot_1.png`, `screenshot_2.png` |
| `{Date}_{Time}_` | `20260212_143025_1.png` |
| `img_{Date}_` | `img_20260212_1.png` |

## 编译

需要 CMake 和 Visual Studio 2022：

```bash
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

生成文件：
- `build\Release\AdvancedPaste.exe` — 托盘 GUI 主程序
- `build\Release\AdvancedPasteCli.exe` — 命令行工具（见下一节）

## 命令行工具 (AdvancedPasteCli)

独立的 console 程序，用于脚本化截图、读写配置、模拟键鼠。主要服务于自动化测试与调试（例如让 AI 助手/脚本可重现地触发并记录 overlay 的每一帧状态）。

### 子命令

| 命令 | 作用 |
|---|---|
| `config show` \| `config get KEY` \| `config set KEY VALUE` | 读写 `config.ini` |
| `list-monitors` | JSON 输出所有显示器（index/坐标/primary/dpi） |
| `capture --fullscreen` \| `--monitor N` \| `--rect X,Y,W,H` `--out FILE` | 无交互截图存 PNG |
| `capture ... --loop N --interval MS --out-dir DIR [--out-prefix P]` | 按间隔连拍序列帧 |
| `render-overlay --rect X,Y,W,H --out FILE [--monitor N]` | 离屏复现 overlay 选区视觉（屏幕快照 + 暗遮罩 + 选区原图 + 橙色渐变边框） |
| `send-keys <COMBO>` | 模拟键盘（如 `ctrl+alt+x`、`esc`、`f5`） |
| `send-mouse move` \| `click` \| `down` \| `up` \| `drag` `[--button B]` `[--monitor N]` | 模拟鼠标 |

### 典型用法：自动化采集 overlay 全过程

开连拍、模拟快捷键、模拟框选、ESC 取消——记录 overlay 从出现到销毁的每一帧：

```bash
CLI="build/Release/AdvancedPasteCli.exe"
OUT="build/Release/e2e_out"
mkdir -p "$OUT"

# 后台连拍 15 帧 × 250ms ≈ 3.75 秒
"$CLI" capture --monitor 1 --loop 15 --interval 250 --out-dir "$OUT" --out-prefix f_ &
LOOP_PID=$!

sleep 0.3
"$CLI" send-keys "ctrl+alt+x"
sleep 0.6
"$CLI" send-mouse drag 400,300 1300,800 --monitor 1
sleep 0.8
"$CLI" send-keys "esc"

wait $LOOP_PID
```

上面的流程已经封装成可复跑脚本：
- `scripts/e2e_overlay_capture.sh` — 跨屏截图流程（git-bash 下运行）
- `scripts/e2e_overlay_capture.bat` — 同上的 Windows cmd 版本
- `scripts/e2e_overlay_hover.bat` — 单屏 hover 高亮流程，观察光标在不同位置时窗口高亮的变化
- `scripts/run_all_tests.bat` — 一键串起 build + 单元测试 + 上述所有 e2e 脚本，末尾输出 PASS/FAIL 汇总，退出码非 0 表示失败（可接 CI）

脚本会自动启动 GUI（如未运行）、连拍、注入键鼠，并在结束时检查帧数与尺寸分段判断 overlay 是否被成功触发。运行期间屏幕会短暂出现 overlay（约 3-5 秒），期间请勿移动鼠标键盘。

### 坐标系约定

- `capture --rect` / `send-mouse move X,Y`：默认为**全局虚拟屏坐标**（多屏时可为负）
- 指定 `--monitor N` 后：坐标视作**该屏局部坐标**（左上角为 0,0），CLI 自动偏移
- 先用 `list-monitors` 查看显示器 index 与坐标

不带参数或 `--help` 查看完整帮助。
