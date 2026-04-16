// AdvancedPasteCli.exe —— 命令行工具，与 GUI 主程序解耦。
// Console subsystem：stdout 在 cmd/bash 下都原生工作，无需 AttachConsole。
#include <windows.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include "utils.h"
#include "render.h"
#include "cli_args.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shlwapi.lib")

// ===== 输出辅助：控制台走 WriteConsoleW，重定向走 UTF-8 =====
static void WriteUtf8(HANDLE h, const wchar_t* s, size_t n) {
    int u8len = WideCharToMultiByte(CP_UTF8, 0, s, (int)n, NULL, 0, NULL, NULL);
    if (u8len <= 0) return;
    std::string u8((size_t)u8len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, (int)n, u8.data(), u8len, NULL, NULL);
    DWORD written;
    WriteFile(h, u8.data(), (DWORD)u8.size(), &written, NULL);
}

static void WriteHandleW(DWORD stdId, const std::wstring& s, bool addNewline) {
    HANDLE h = GetStdHandle(stdId);
    if (!h || h == INVALID_HANDLE_VALUE) return;
    std::wstring line = addNewline ? s + L"\n" : s;
    DWORD mode, written;
    if (GetConsoleMode(h, &mode))
        WriteConsoleW(h, line.c_str(), (DWORD)line.size(), &written, NULL);
    else
        WriteUtf8(h, line.c_str(), line.size());
}

static void WriteOutLn(const std::wstring& s) { WriteHandleW(STD_OUTPUT_HANDLE, s, true); }
static void WriteErrLn(const std::wstring& s) { WriteHandleW(STD_ERROR_HANDLE, s, true); }

// JSON 转义、ParseRect / ParseXY / ParseKeyName / ParseButton /
// FindOpt / HasFlag 都在 cli_args.h 里共享（方便 gtest 覆盖）。

static std::wstring ToAbsPath(const std::wstring& path) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetFullPathNameW(path.c_str(), MAX_PATH, buf, NULL);
    if (n == 0 || n >= MAX_PATH) return path;
    return std::wstring(buf, n);
}

// ===== config 子命令 =====
// GUI 主程序的 config.ini 位于主 exe 同目录；CLI 独立后，ini 路径应与 GUI 主 exe 对齐。
// utils::GetConfigPath() 返回的是当前 exe 的同目录 —— CLI 和 GUI 通常一起发布在同一目录，
// 所以此处直接复用即可；如果拆开目录，需传 --config-path 参数覆盖（暂未实现，按需加）。

static const wchar_t* kKnownKeys[] = {
    L"Hotkey", L"Pattern", L"AutoFinishOnSelect", L"SaveDir", L"DelaySeconds"
};

static std::wstring ReadIniRaw(const wchar_t* key) {
    std::wstring cfgPath = GetConfigPath();
    const wchar_t* SENT = L"\x01__ABSENT__\x01";
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Settings", key, SENT, buf, MAX_PATH, cfgPath.c_str());
    if (wcscmp(buf, SENT) == 0) return L"";
    return std::wstring(buf);
}

static int CmdConfigShow() {
    WriteOutLn(L"{");
    constexpr size_t N = sizeof(kKnownKeys) / sizeof(kKnownKeys[0]);
    for (size_t i = 0; i < N; i++) {
        std::wstring v = ReadIniRaw(kKnownKeys[i]);
        std::wstring line = L"  \"";
        line += kKnownKeys[i];
        line += L"\": \"";
        line += JsonEscape(v);
        line += L"\"";
        if (i + 1 < N) line += L",";
        WriteOutLn(line);
    }
    WriteOutLn(L"}");
    return 0;
}

static int CmdConfigGet(const wchar_t* key) {
    if (!key) { WriteErrLn(L"config get: missing KEY"); return 2; }
    WriteOutLn(ReadIniRaw(key));
    return 0;
}

static int CmdConfigSet(const wchar_t* key, const wchar_t* value) {
    if (!key || !value) { WriteErrLn(L"config set: missing KEY or VALUE"); return 2; }
    BOOL ok = WritePrivateProfileStringW(L"Settings", key, value, GetConfigPath().c_str());
    return ok ? 0 : 1;
}

static int CmdConfig(int argc, wchar_t** argv) {
    if (argc < 3) { WriteErrLn(L"usage: config show|get KEY|set KEY VALUE"); return 2; }
    const wchar_t* sub = argv[2];
    if (wcscmp(sub, L"show") == 0) return CmdConfigShow();
    if (wcscmp(sub, L"get")  == 0) return CmdConfigGet(argc >= 4 ? argv[3] : NULL);
    if (wcscmp(sub, L"set")  == 0) return CmdConfigSet(argc >= 4 ? argv[3] : NULL,
                                                       argc >= 5 ? argv[4] : NULL);
    WriteErrLn(std::wstring(L"unknown config subcommand: ") + sub);
    return 2;
}

// ===== list-monitors 子命令 =====
struct MonEntry { int idx; RECT rc; bool primary; UINT dpiX, dpiY; };

static BOOL CALLBACK EnumMonCb(HMONITOR h, HDC, LPRECT, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<MonEntry>*>(lp);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(h, &mi);
    MonEntry m{};
    m.idx = (int)v->size();
    m.rc = mi.rcMonitor;
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    m.dpiX = m.dpiY = 96;
    HMODULE hShcore = LoadLibraryW(L"shcore.dll");
    if (hShcore) {
        typedef HRESULT (WINAPI *PFN)(HMONITOR, int, UINT*, UINT*);
        auto pGet = (PFN)GetProcAddress(hShcore, "GetDpiForMonitor");
        if (pGet) pGet(h, 0 /*MDT_EFFECTIVE_DPI*/, &m.dpiX, &m.dpiY);
        FreeLibrary(hShcore);
    }
    v->push_back(m);
    return TRUE;
}

static int CmdListMonitors() {
    std::vector<MonEntry> mons;
    EnumDisplayMonitors(NULL, NULL, EnumMonCb, (LPARAM)&mons);
    WriteOutLn(L"[");
    for (size_t i = 0; i < mons.size(); i++) {
        const MonEntry& m = mons[i];
        wchar_t buf[256];
        swprintf_s(buf, 256,
            L"  { \"index\": %d, \"x\": %ld, \"y\": %ld, \"w\": %ld, \"h\": %ld, "
            L"\"primary\": %s, \"dpi\": %u }%s",
            m.idx, m.rc.left, m.rc.top,
            m.rc.right - m.rc.left, m.rc.bottom - m.rc.top,
            m.primary ? L"true" : L"false", m.dpiX,
            i + 1 < mons.size() ? L"," : L"");
        WriteOutLn(buf);
    }
    WriteOutLn(L"]");
    return 0;
}

// ===== capture 子命令 =====
static int SaveRectPng(int x, int y, int w, int h, const std::wstring& outPath) {
    if (w <= 0 || h <= 0) { WriteErrLn(L"capture: invalid size"); return 2; }
    HDC hdcScr = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScr);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScr, w, h);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcMem, hBmp);
    BitBlt(hdcMem, 0, 0, w, h, hdcScr, x, y, SRCCOPY);
    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScr);

    int rc = 1;
    {
        Gdiplus::Bitmap bmp(hBmp, NULL);
        CLSID clsid;
        if (GetEncoderClsid(L"image/png", &clsid) >= 0 &&
            bmp.Save(outPath.c_str(), &clsid, NULL) == Gdiplus::Ok) {
            rc = 0;
        }
    }
    DeleteObject(hBmp);
    if (rc == 0) WriteOutLn(outPath);
    else         WriteErrLn(L"capture: failed to save PNG to " + outPath);
    return rc;
}

// 从 --fullscreen / --monitor N / --rect X,Y,W,H 解析出目标矩形（全局虚拟屏坐标）
static bool ParseCaptureRange(int argc, wchar_t** argv, int* x, int* y, int* w, int* h) {
    if (HasFlag(argc, argv, L"--fullscreen")) {
        *x = GetSystemMetrics(SM_XVIRTUALSCREEN);
        *y = GetSystemMetrics(SM_YVIRTUALSCREEN);
        *w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        *h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return true;
    }
    if (const wchar_t* monOpt = FindOpt(argc, argv, L"--monitor")) {
        int idx = _wtoi(monOpt);
        std::vector<MonEntry> mons;
        EnumDisplayMonitors(NULL, NULL, EnumMonCb, (LPARAM)&mons);
        if (idx < 0 || idx >= (int)mons.size()) {
            WriteErrLn(L"capture: --monitor index out of range");
            return false;
        }
        const RECT& r = mons[idx].rc;
        *x = r.left; *y = r.top;
        *w = r.right - r.left; *h = r.bottom - r.top;
        return true;
    }
    if (const wchar_t* rectOpt = FindOpt(argc, argv, L"--rect")) {
        if (!ParseRect(rectOpt, x, y, w, h)) {
            WriteErrLn(L"capture: --rect expects X,Y,W,H");
            return false;
        }
        return true;
    }
    WriteErrLn(L"capture: need --fullscreen / --monitor N / --rect X,Y,W,H");
    return false;
}

static int CmdCapture(int argc, wchar_t** argv) {
    int x, y, w, h;
    if (!ParseCaptureRange(argc, argv, &x, &y, &w, &h)) return 2;

    // --loop 模式：按间隔连拍多张 PNG 到目录
    if (const wchar_t* loopOpt = FindOpt(argc, argv, L"--loop")) {
        int count = _wtoi(loopOpt);
        if (count <= 0) { WriteErrLn(L"capture: --loop expects positive N"); return 2; }
        int interval = 200;
        if (const wchar_t* v = FindOpt(argc, argv, L"--interval")) interval = _wtoi(v);
        if (interval < 0) interval = 0;

        const wchar_t* dirOpt = FindOpt(argc, argv, L"--out-dir");
        if (!dirOpt) { WriteErrLn(L"capture --loop: --out-dir DIR required"); return 2; }
        std::wstring dir = ToAbsPath(dirOpt);
        CreateDirectoryW(dir.c_str(), NULL); // 已存在会失败但无害
        if (!dir.empty() && dir.back() != L'\\' && dir.back() != L'/') dir += L'\\';

        std::wstring prefix = L"shot_";
        if (const wchar_t* v = FindOpt(argc, argv, L"--out-prefix")) prefix = v;

        for (int i = 0; i < count; i++) {
            wchar_t name[32];
            swprintf_s(name, 32, L"%03d.png", i);
            std::wstring path = dir + prefix + name;
            int rc = SaveRectPng(x, y, w, h, path);
            if (rc != 0) return rc;
            if (i + 1 < count && interval > 0) Sleep(interval);
        }
        return 0;
    }

    // 单次模式
    const wchar_t* out = FindOpt(argc, argv, L"--out");
    if (!out) { WriteErrLn(L"capture: --out PATH required (or --loop with --out-dir)"); return 2; }
    return SaveRectPng(x, y, w, h, ToAbsPath(out));
}

// ===== render-overlay 子命令 =====
// 复现 overlay 的选区效果（屏幕快照 + 半透明遮罩 + 选区原图 + 橙色渐变边框），
// 产出该 rect 所在显示器整张屏幕的 PNG。默认边框参数与 GUI overlay 一致。
static int CmdRenderOverlay(int argc, wchar_t** argv) {
    const wchar_t* out = FindOpt(argc, argv, L"--out");
    const wchar_t* rectOpt = FindOpt(argc, argv, L"--rect");
    if (!out)     { WriteErrLn(L"render-overlay: --out PATH required"); return 2; }
    if (!rectOpt) { WriteErrLn(L"render-overlay: --rect X,Y,W,H required"); return 2; }
    int sx, sy, sw, sh;
    if (!ParseRect(rectOpt, &sx, &sy, &sw, &sh) || sw <= 0 || sh <= 0) {
        WriteErrLn(L"render-overlay: --rect expects X,Y,W,H with positive W/H");
        return 2;
    }
    std::wstring outAbs = ToAbsPath(out);

    // 可覆盖参数（不给就用 overlay 的默认值）
    int borderW = 150;
    BYTE maxAlpha = 180;
    COLORREF color = RGB(255, 160, 40);
    int maskAlpha = 100;
    if (const wchar_t* v = FindOpt(argc, argv, L"--border-width")) borderW = _wtoi(v);
    if (const wchar_t* v = FindOpt(argc, argv, L"--border-alpha")) maxAlpha = (BYTE)_wtoi(v);
    if (const wchar_t* v = FindOpt(argc, argv, L"--mask-alpha"))   maskAlpha = _wtoi(v);
    if (const wchar_t* v = FindOpt(argc, argv, L"--color")) {
        int r, g, b;
        if (swscanf_s(v, L"%d,%d,%d", &r, &g, &b) == 3) color = RGB(r, g, b);
    }

    // --monitor N 指定显示器：rect 视作该屏局部坐标（左上角 0,0）
    // 不指定：rect 视作全局虚拟屏坐标，由中心点自动判定所在屏
    RECT monRc;
    if (const wchar_t* monOpt = FindOpt(argc, argv, L"--monitor")) {
        int idx = _wtoi(monOpt);
        std::vector<MonEntry> mons;
        EnumDisplayMonitors(NULL, NULL, EnumMonCb, (LPARAM)&mons);
        if (idx < 0 || idx >= (int)mons.size()) {
            WriteErrLn(L"render-overlay: --monitor index out of range");
            return 2;
        }
        monRc = mons[idx].rc;
        // 把 rect 从该屏局部坐标转为全局坐标
        sx += monRc.left;
        sy += monRc.top;
    } else {
        POINT pt = { sx + sw / 2, sy + sh / 2 };
        HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfo(hMon, &mi);
        monRc = mi.rcMonitor;
    }
    int mx = monRc.left, my = monRc.top;
    int mw = monRc.right - mx, mh = monRc.bottom - my;

    HDC hdcScr = GetDC(NULL);
    HDC hdcCanvas = CreateCompatibleDC(hdcScr);
    HBITMAP hCanvas = CreateCompatibleBitmap(hdcScr, mw, mh);
    HBITMAP hCanvasOld = (HBITMAP)SelectObject(hdcCanvas, hCanvas);
    BitBlt(hdcCanvas, 0, 0, mw, mh, hdcScr, mx, my, SRCCOPY);

    // 备份原屏（用于选区抠回）
    HDC hdcOrig = CreateCompatibleDC(hdcScr);
    HBITMAP hOrig = CreateCompatibleBitmap(hdcScr, mw, mh);
    HBITMAP hOrigOld = (HBITMAP)SelectObject(hdcOrig, hOrig);
    BitBlt(hdcOrig, 0, 0, mw, mh, hdcScr, mx, my, SRCCOPY);

    // 全屏半透明黑遮罩
    HDC hdcDim = CreateCompatibleDC(hdcScr);
    HBITMAP hDim = CreateCompatibleBitmap(hdcScr, mw, mh);
    HBITMAP hDimOld = (HBITMAP)SelectObject(hdcDim, hDim);
    HBRUSH hBr = CreateSolidBrush(RGB(0, 0, 0));
    RECT rcF = { 0, 0, mw, mh };
    FillRect(hdcDim, &rcF, hBr);
    DeleteObject(hBr);
    BLENDFUNCTION bfDim = { AC_SRC_OVER, 0, (BYTE)maskAlpha, 0 };
    AlphaBlend(hdcCanvas, 0, 0, mw, mh, hdcDim, 0, 0, mw, mh, bfDim);
    SelectObject(hdcDim, hDimOld); DeleteObject(hDim); DeleteDC(hdcDim);

    // 选区：从原图抠回，然后叠橙色渐变边框
    RECT sel = { sx - mx, sy - my, sx - mx + sw, sy - my + sh };
    sel.left   = (std::max)(0L, sel.left);
    sel.top    = (std::max)(0L, sel.top);
    sel.right  = (std::min)((LONG)mw, sel.right);
    sel.bottom = (std::min)((LONG)mh, sel.bottom);
    int selW = sel.right - sel.left, selH = sel.bottom - sel.top;
    if (selW > 0 && selH > 0) {
        BitBlt(hdcCanvas, sel.left, sel.top, selW, selH, hdcOrig, sel.left, sel.top, SRCCOPY);
        DrawGradientBorder(hdcCanvas, sel, color, borderW, maxAlpha);
    }

    SelectObject(hdcOrig, hOrigOld); DeleteObject(hOrig); DeleteDC(hdcOrig);
    ReleaseDC(NULL, hdcScr);

    SelectObject(hdcCanvas, hCanvasOld);
    int rc = 1;
    {
        Gdiplus::Bitmap bmp(hCanvas, NULL);
        CLSID clsid;
        if (GetEncoderClsid(L"image/png", &clsid) >= 0 &&
            bmp.Save(outAbs.c_str(), &clsid, NULL) == Gdiplus::Ok) {
            rc = 0;
        }
    }
    DeleteDC(hdcCanvas);
    DeleteObject(hCanvas);
    if (rc == 0) WriteOutLn(outAbs);
    else         WriteErrLn(L"render-overlay: failed to save PNG to " + outAbs);
    return rc;
}

// ===== send-keys 子命令 =====
static int CmdSendKeys(int argc, wchar_t** argv) {
    if (argc < 3) {
        WriteErrLn(L"send-keys: combo required (e.g. 'ctrl+alt+x')");
        return 2;
    }
    std::vector<WORD> vks;
    std::wstring cur;
    std::wstring s = argv[2];
    auto pushCur = [&]() -> bool {
        if (cur.empty()) return true;
        WORD vk;
        if (!ParseKeyName(cur, &vk)) {
            WriteErrLn(std::wstring(L"send-keys: unknown key: ") + cur);
            return false;
        }
        vks.push_back(vk);
        cur.clear();
        return true;
    };
    for (wchar_t c : s) {
        if (c == L'+') { if (!pushCur()) return 2; }
        else if (c != L' ') cur += c;
    }
    if (!pushCur()) return 2;
    if (vks.empty()) { WriteErrLn(L"send-keys: empty combo"); return 2; }

    std::vector<INPUT> seq;
    seq.reserve(vks.size() * 2);
    for (WORD vk : vks) {
        INPUT in = {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = vk;
        seq.push_back(in);
    }
    for (auto it = vks.rbegin(); it != vks.rend(); ++it) {
        INPUT in = {};
        in.type = INPUT_KEYBOARD;
        in.ki.wVk = *it;
        in.ki.dwFlags = KEYEVENTF_KEYUP;
        seq.push_back(in);
    }
    UINT sent = SendInput((UINT)seq.size(), seq.data(), sizeof(INPUT));
    if (sent != seq.size()) {
        WriteErrLn(L"send-keys: SendInput was blocked or partially sent");
        return 1;
    }
    return 0;
}

// ===== send-mouse 子命令 =====
static void InjectMouseMove(int absX, int absY) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dx = (LONG)((absX - vx) * 65535LL / (vw > 1 ? vw - 1 : 1));
    in.mi.dy = (LONG)((absY - vy) * 65535LL / (vh > 1 ? vh - 1 : 1));
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    SendInput(1, &in, sizeof(in));
}
static void InjectMouseBtn(DWORD flag) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof(in));
}
// 若指定了 --monitor，则把 x,y 从该屏局部坐标偏移到全局虚拟屏坐标
static bool ApplyMonitorOffset(int argc, wchar_t** argv, int* x, int* y) {
    const wchar_t* monOpt = FindOpt(argc, argv, L"--monitor");
    if (!monOpt) return true;
    int idx = _wtoi(monOpt);
    std::vector<MonEntry> mons;
    EnumDisplayMonitors(NULL, NULL, EnumMonCb, (LPARAM)&mons);
    if (idx < 0 || idx >= (int)mons.size()) {
        WriteErrLn(L"send-mouse: --monitor index out of range");
        return false;
    }
    *x += mons[idx].rc.left;
    *y += mons[idx].rc.top;
    return true;
}

static int CmdSendMouse(int argc, wchar_t** argv) {
    if (argc < 3) { WriteErrLn(L"send-mouse: action required (move/click/down/up/drag)"); return 2; }
    const wchar_t* action = argv[2];

    DWORD btnDown = MOUSEEVENTF_LEFTDOWN, btnUp = MOUSEEVENTF_LEFTUP;
    if (const wchar_t* v = FindOpt(argc, argv, L"--button")) {
        if (!ParseButton(v, &btnDown, &btnUp)) {
            WriteErrLn(L"send-mouse: --button expects left/right/middle");
            return 2;
        }
    }

    if (wcscmp(action, L"move") == 0 || wcscmp(action, L"click") == 0) {
        if (argc < 4) { WriteErrLn(L"send-mouse: X,Y required"); return 2; }
        int x, y;
        if (!ParseXY(argv[3], &x, &y)) { WriteErrLn(L"send-mouse: invalid X,Y"); return 2; }
        if (!ApplyMonitorOffset(argc, argv, &x, &y)) return 2;
        InjectMouseMove(x, y);
        if (wcscmp(action, L"click") == 0) {
            Sleep(30);
            InjectMouseBtn(btnDown);
            Sleep(30);
            InjectMouseBtn(btnUp);
        }
        return 0;
    }
    if (wcscmp(action, L"down") == 0) { InjectMouseBtn(btnDown); return 0; }
    if (wcscmp(action, L"up")   == 0) { InjectMouseBtn(btnUp);   return 0; }
    if (wcscmp(action, L"drag") == 0) {
        if (argc < 5) { WriteErrLn(L"send-mouse drag: X1,Y1 X2,Y2 required"); return 2; }
        int x1, y1, x2, y2;
        if (!ParseXY(argv[3], &x1, &y1) || !ParseXY(argv[4], &x2, &y2)) {
            WriteErrLn(L"send-mouse drag: invalid coordinates");
            return 2;
        }
        if (!ApplyMonitorOffset(argc, argv, &x1, &y1)) return 2;
        if (!ApplyMonitorOffset(argc, argv, &x2, &y2)) return 2;
        InjectMouseMove(x1, y1);
        Sleep(50);
        InjectMouseBtn(btnDown);
        Sleep(50);
        // 中间插值，让 overlay 的 WM_MOUSEMOVE 多次触发
        const int STEPS = 8;
        for (int i = 1; i <= STEPS; i++) {
            int mx = x1 + (x2 - x1) * i / STEPS;
            int my = y1 + (y2 - y1) * i / STEPS;
            InjectMouseMove(mx, my);
            Sleep(20);
        }
        InjectMouseBtn(btnUp);
        return 0;
    }
    WriteErrLn(std::wstring(L"send-mouse: unknown action: ") + action);
    return 2;
}

// ===== help =====
static int CmdHelp() {
    WriteOutLn(L"AdvancedPasteCli — 命令行工具");
    WriteOutLn(L"用法:");
    WriteOutLn(L"  AdvancedPasteCli config show");
    WriteOutLn(L"  AdvancedPasteCli config get <KEY>");
    WriteOutLn(L"  AdvancedPasteCli config set <KEY> <VALUE>");
    WriteOutLn(L"  AdvancedPasteCli list-monitors");
    WriteOutLn(L"  AdvancedPasteCli capture --fullscreen --out <PNG>");
    WriteOutLn(L"  AdvancedPasteCli capture --monitor <N> --out <PNG>");
    WriteOutLn(L"  AdvancedPasteCli capture --rect <X,Y,W,H> --out <PNG>");
    WriteOutLn(L"  AdvancedPasteCli capture [--fullscreen|--monitor N|--rect X,Y,W,H]");
    WriteOutLn(L"      --loop N --interval MS --out-dir <DIR> [--out-prefix P]");
    WriteOutLn(L"  AdvancedPasteCli send-keys <combo>   # e.g. 'ctrl+alt+x', 'esc', 'f5'");
    WriteOutLn(L"  AdvancedPasteCli send-mouse move <X,Y> [--monitor N]");
    WriteOutLn(L"  AdvancedPasteCli send-mouse click <X,Y> [--button left|right|middle] [--monitor N]");
    WriteOutLn(L"  AdvancedPasteCli send-mouse down|up [--button B]");
    WriteOutLn(L"  AdvancedPasteCli send-mouse drag <X1,Y1> <X2,Y2> [--button B] [--monitor N]");
    WriteOutLn(L"      [--monitor N] [--color R,G,B] [--border-width N]");
    WriteOutLn(L"      [--border-alpha N] [--mask-alpha N]");
    WriteOutLn(L"      指定 --monitor 时 rect 为该屏局部坐标（0,0 为屏幕左上角），");
    WriteOutLn(L"      否则 rect 为全局虚拟屏坐标。");
    WriteOutLn(L"");
    WriteOutLn(L"配置文件:  与 AdvancedPaste.exe 同目录下的 config.ini");
    WriteOutLn(L"KEYS:      Hotkey, Pattern, AutoFinishOnSelect, SaveDir, DelaySeconds");
    return 0;
}

// ===== 入口 =====
int wmain(int argc, wchar_t** argv) {
    // Per-Monitor DPI V2（与 GUI 主程序保持一致，避免截图被缩放）
    typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    auto pSetCtx = hUser ? (PFN_SetProcessDpiAwarenessContext)
        GetProcAddress(hUser, "SetProcessDpiAwarenessContext") : NULL;
    if (!pSetCtx || !pSetCtx((HANDLE)-4 /* PER_MONITOR_AWARE_V2 */))
        SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput gdipIn;
    ULONG_PTR gdipTk;
    Gdiplus::GdiplusStartup(&gdipTk, &gdipIn, NULL);

    int rc;
    if (argc < 2) {
        rc = CmdHelp();
    } else {
        const wchar_t* cmd = argv[1];
        if (wcscmp(cmd, L"help") == 0 || wcscmp(cmd, L"--help") == 0 || wcscmp(cmd, L"-h") == 0)
            rc = CmdHelp();
        else if (wcscmp(cmd, L"config") == 0)        rc = CmdConfig(argc, argv);
        else if (wcscmp(cmd, L"list-monitors") == 0) rc = CmdListMonitors();
        else if (wcscmp(cmd, L"capture") == 0)       rc = CmdCapture(argc, argv);
        else if (wcscmp(cmd, L"render-overlay") == 0) rc = CmdRenderOverlay(argc, argv);
        else if (wcscmp(cmd, L"send-keys") == 0)     rc = CmdSendKeys(argc, argv);
        else if (wcscmp(cmd, L"send-mouse") == 0)    rc = CmdSendMouse(argc, argv);
        else {
            WriteErrLn(std::wstring(L"unknown command: ") + cmd);
            rc = 2;
        }
    }

    Gdiplus::GdiplusShutdown(gdipTk);
    return rc;
}
