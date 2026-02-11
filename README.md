# Advanced Paste 截图工具

## 快捷键

- **Ctrl+Alt+X** — 启动截图
- **ESC** — 取消截图
- **Ctrl+Z / 右键** — 撤销最后一个标注

## 使用流程

1. 运行 `AdvancedPaste.exe`，程序常驻系统托盘
2. 按 `Ctrl+Alt+X` 进入截图模式，屏幕冻结并变暗
3. **选择区域**：
   - 移动鼠标，自动高亮光标所在窗口，单击即可选中
   - 或按住左键拖拽，自由选择矩形区域
4. **标注编辑**（可选）：
   - 选区下方出现工具栏：「箭头」「矩形」「完成」「取消」
   - 点击「箭头」或「矩形」激活工具，在选区内拖拽绘制
   - 右键或 Ctrl+Z 撤销上一个标注
5. 点击「完成」，截图（含标注）保存到内存，同时写入系统剪贴板
6. 打开任意文件夹，按 `Ctrl+V` 粘贴为 PNG 文件，自动命名为 `1.png`、`2.png`...（根据目标文件夹已有文件递增）
7. 粘贴完成后自动恢复正常的复制粘贴功能；截图也可在其他应用中通过 `Ctrl+V` 粘贴为图片

## 退出

右键系统托盘图标 → 「退出」

## 编译

需要 CMake 和 Visual Studio 2022：

```bash
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

生成文件：`build\Release\AdvancedPaste.exe`
