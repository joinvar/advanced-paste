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
SaveDir=
```

**Hotkey** - Capture hotkey. Supports modifier keys `Ctrl`, `Alt`, `Shift`, and `Win`, joined by `+`. The last part must be the main key (`A-Z`, `0-9`, `F1-F24`). Restart the app after changing it. Even if the hotkey is occupied by the system (such as `Shift+Win+S`), the app still tries to intercept it through a keyboard hook.

**Pattern** - Output filename prefix. Can be empty. Supported placeholders:
- `{Date}` - Current date in `20260212` format
- `{Time}` - Current time in `143025` format

**AutoFinishOnSelect** - Whether to finish immediately after selection:
- `0` (default) - Enter edit mode after selecting, then finish by clicking "Done" or right-clicking outside the selection
- `1` - Finish immediately on left-button release and close the overlay without entering edit mode

**SaveDir** - Auto-save directory. Can be empty. Supports environment variables (e.g. `%USERPROFILE%\Pictures`); the directory is created if missing:
- _empty_ (default) - Keep the original flow: the screenshot goes to the clipboard, and `Ctrl+V` in File Explorer pastes it as a PNG
- _non-empty_ - Once a capture finishes, the PNG is immediately saved into this directory using the `Pattern` naming scheme, and the **absolute file path** is placed on the clipboard as text so `Ctrl+V` in a terminal pastes the path directly. The bitmap format is still on the clipboard, so pasting into chat/image apps still works as an image

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

Outputs:
- `build\Release\AdvancedPaste.exe` — GUI tray app
- `build\Release\AdvancedPasteCli.exe` — Command-line tool (see the next section)

## Command-Line Tool (AdvancedPasteCli)

A standalone console program for scripted screenshots, config access, and input injection. Primarily aimed at automated testing and debugging (e.g. letting AI assistants / scripts reproducibly trigger and record every frame of the overlay).

### Subcommands

| Command | Purpose |
|---|---|
| `config show` \| `config get KEY` \| `config set KEY VALUE` | Read/write `config.ini` |
| `list-monitors` | JSON list of all monitors (index/coords/primary/dpi) |
| `capture --fullscreen` \| `--monitor N` \| `--rect X,Y,W,H` `--out FILE` | Non-interactive screenshot to PNG |
| `capture ... --loop N --interval MS --out-dir DIR [--out-prefix P]` | Time-lapse frame sequence |
| `render-overlay --rect X,Y,W,H --out FILE [--monitor N]` | Offscreen rendering of the overlay selection look (screen snapshot + dim mask + selection cutout + orange gradient border) |
| `send-keys <COMBO>` | Keyboard injection (e.g. `ctrl+alt+x`, `esc`, `f5`) |
| `send-mouse move` \| `click` \| `down` \| `up` \| `drag` `[--button B]` `[--monitor N]` | Mouse injection |

### Example: automated overlay capture flow

Start the loop capture, inject the hotkey, simulate a drag selection, then press ESC — every frame from overlay appearance to teardown is recorded:

```bash
CLI="build/Release/AdvancedPasteCli.exe"
OUT="build/Release/e2e_out"
mkdir -p "$OUT"

# 15 frames x 250 ms ~= 3.75 s in the background
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

### Coordinate conventions

- `capture --rect` / `send-mouse move X,Y`: **global virtual-desktop** coordinates by default (can be negative on multi-monitor setups)
- With `--monitor N`: coordinates are treated as **monitor-local** (with `(0, 0)` at the monitor's top-left); the CLI applies the offset automatically
- Use `list-monitors` to check monitor indices and coordinates first

Run with no arguments or `--help` for the full usage.
