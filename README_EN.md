# Advanced Paste Screenshot Tool

[中文](README.md) | English

## Hotkeys

- **Ctrl+Alt+X** (default, configurable) - Start capture
- **ESC** - Cancel capture
- **Ctrl+Z / Right-click inside selection** - Undo the last annotation
- **Right-click outside selection** - Finish capture (same as clicking "Done")

## Workflow

1. Run `AdvancedPaste.exe`; the app stays in the system tray
2. Press `Ctrl+Alt+X` to enter capture mode; the screen freezes and dims
3. **Select an area**:
   - Move the mouse to auto-highlight the window under the cursor, then click to select it
   - Or drag with the left mouse button to select any rectangular region
4. **Annotate** (optional):
   - A toolbar appears below the selection: "Arrow", "Rectangle", "Brush", "Done", "Cancel"
   - Click "Arrow", "Rectangle" or "Brush" to activate the tool, then drag inside the selection to draw
   - "Brush" supports freehand curves — hold the left button and move
   - Right-click inside the selection or press Ctrl+Z to undo the last annotation
   - Right-click outside the selection to finish immediately
5. Click "Done" (or right-click outside the selection) to store the screenshot with annotations in memory and copy it to the system clipboard
6. In a real folder tab in File Explorer, press `Ctrl+V` to paste the screenshot as a PNG file; the filename is generated from the `Pattern` setting in `config.ini`
7. After the paste completes, normal copy/paste behavior is restored; the screenshot can also be pasted into other apps as an image with `Ctrl+V`

Notes:

- File Explorer tabbed windows are supported; the currently active folder tab is used as the save target
- Non-folder locations such as `Home`, `Quick Access`, and `This PC` are not valid save targets

## Exit

Right-click the tray icon -> `Exit`

## Config File

On first launch, the program automatically creates `config.ini` next to the executable:

```ini
[Settings]
Hotkey=Ctrl+Alt+X
Pattern=
AutoFinishOnSelect=0
```

**Hotkey** - Capture hotkey. Supports modifier keys `Ctrl`, `Alt`, `Shift`, and `Win`, joined by `+`. The last part must be the main key (`A-Z`, `0-9`, `F1-F24`). Restart the app after changing it. Even if the hotkey is occupied by the system (such as `Shift+Win+S`), the app still tries to intercept it through a keyboard hook.

**Pattern** - Output filename prefix. Can be empty. Supported placeholders:
- `{Date}` - Current date in `20260212` format
- `{Time}` - Current time in `143025` format

**AutoFinishOnSelect** - Whether to finish immediately after selection:
- `0` (default) - Enter edit mode after selecting, then finish by clicking "Done" or right-clicking outside the selection
- `1` - Finish immediately on left-button release and close the overlay without entering edit mode

Examples:

| Pattern | Generated filename |
|---|---|
| _(empty)_ | `1.png`, `2.png` |
| `screenshot_` | `screenshot_1.png`, `screenshot_2.png` |
| `{Date}_{Time}_` | `20260212_143025_1.png` |
| `img_{Date}_` | `img_20260212_1.png` |

## Build

Requires CMake and Visual Studio 2022:

```bash
mkdir build && cd build
cmake -G "Visual Studio 17 2022" -A x64 ..
cmake --build . --config Release
```

Output: `build\Release\AdvancedPaste.exe`
