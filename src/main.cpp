#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include "overlay.h"
#include "resource.h"
#include "utils.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON           (WM_USER + 1)
#define WM_DO_PASTE           (WM_USER + 2)
#define ID_HOTKEY             1
#define ID_TRAY_EXIT          1001
#define ID_TRAY_SET_SAVEDIR   1002
#define ID_TRAY_CLEAR_SAVEDIR 1003
#define ID_TRAY_TOGGLE_AUTOFINISH 1004
#define ID_TRAY_OPEN_CONFIG   1005
#define ID_TRAY_DELAY_CAPTURE 1006

static NOTIFYICONDATAW g_nid = {};
static HWND  g_hwndMain = NULL;
static HHOOK g_hKeyHook = NULL;
static UINT  g_hkMod = 0;   // 快捷键修饰键 (MOD_*)
static UINT  g_hkVk  = 0;   // 快捷键主键
static bool  g_hkUseHook = false; // RegisterHotKey 失败时用钩子拦截

static bool CheckHotkeyModifiers(UINT mod) {
    bool ctrl  = (mod & MOD_CONTROL) ? (GetAsyncKeyState(VK_CONTROL) & 0x8000) : !(GetAsyncKeyState(VK_CONTROL) & 0x8000);
    bool alt   = (mod & MOD_ALT)     ? (GetAsyncKeyState(VK_MENU)    & 0x8000) : !(GetAsyncKeyState(VK_MENU)    & 0x8000);
    bool shift = (mod & MOD_SHIFT)   ? (GetAsyncKeyState(VK_SHIFT)   & 0x8000) : !(GetAsyncKeyState(VK_SHIFT)   & 0x8000);
    bool win   = (mod & MOD_WIN)     ? ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000)
                                     : !((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000);
    return ctrl && alt && shift && win;
}

static void ShowTrayBalloon(const wchar_t* title, const wchar_t* text) {
    NOTIFYICONDATAW nid = g_nid;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid.szInfo, text, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

static HICON LoadAppIcon(HINSTANCE hInstance, int width, int height) {
    HICON hIcon = (HICON)LoadImageW(
        hInstance,
        MAKEINTRESOURCEW(IDI_APP_ICON),
        IMAGE_ICON,
        width,
        height,
        LR_DEFAULTCOLOR
    );
    if (!hIcon)
        hIcon = LoadIconW(NULL, IDI_APPLICATION);
    return hIcon;
}

// 低级键盘钩子：拦截截图快捷键 + Explorer 中的 Ctrl+V
static LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        auto* kb = (KBDLLHOOKSTRUCT*)lParam;
        // 截图快捷键（仅在 RegisterHotKey 失败时启用）
        if (g_hkUseHook && kb->vkCode == g_hkVk && CheckHotkeyModifiers(g_hkMod)) {
            PostMessageW(g_hwndMain, WM_HOTKEY, ID_HOTKEY, 0);
            return 1;
        }
        // Ctrl+V 粘贴拦截
        if (kb->vkCode == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (GetPendingBitmap(NULL, NULL) && IsForegroundExplorerWindow()) {
                PostMessageW(g_hwndMain, WM_DO_PASTE, 0, 0);
                return 1; // 吃掉按键
            }
        }
    }
    return CallNextHookEx(g_hKeyHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_HOTKEY:
        if (wParam == ID_HOTKEY)
            StartCapture(hwnd);
        break;
    case WM_DO_PASTE: {
        int w, h;
        HBITMAP hBmp = GetPendingBitmap(&w, &h);
        if (hBmp) {
            std::wstring dir = GetActiveExplorerPath();
            if (!dir.empty()) {
                SaveBitmapAsPng(hBmp, w, h, dir);
                ClearPendingBitmap();
            }
        }
        break;
    }
    case WM_CLIPBOARDUPDATE:
        OnClipboardChanged();
        return 0;
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);

            std::wstring saveDir = ReadSaveDirConfig();
            bool autoFinish = ReadAutoFinishOnSelectConfig();

            HMENU hMenu = CreatePopupMenu();

            int delaySec = ReadDelaySecondsConfig();
            std::wstring delayLabel = L"延时 " + std::to_wstring(delaySec) + L" 秒截图";
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_DELAY_CAPTURE, delayLabel.c_str());
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

            std::wstring saveDirLabel = L"保存目录: ";
            saveDirLabel += saveDir.empty() ? L"(未设置)" : saveDir;
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_SET_SAVEDIR, saveDirLabel.c_str());
            if (!saveDir.empty())
                AppendMenuW(hMenu, MF_STRING, ID_TRAY_CLEAR_SAVEDIR, L"清除保存目录");

            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu,
                        MF_STRING | (autoFinish ? MF_CHECKED : MF_UNCHECKED),
                        ID_TRAY_TOGGLE_AUTOFINISH, L"框选后立即完成");

            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_OPEN_CONFIG, L"打开配置文件");
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");

            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case ID_TRAY_EXIT:
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
            break;
        case ID_TRAY_SET_SAVEDIR: {
            std::wstring picked = PickFolderDialog(hwnd, ReadSaveDirConfig());
            if (!picked.empty())
                WriteSaveDirConfig(picked);
            break;
        }
        case ID_TRAY_CLEAR_SAVEDIR:
            WriteSaveDirConfig(L"");
            break;
        case ID_TRAY_TOGGLE_AUTOFINISH: {
            bool next = !ReadAutoFinishOnSelectConfig();
            WriteAutoFinishOnSelectConfig(next);
            ShowTrayBalloon(L"截图工具",
                next ? L"框选后立即完成：已开启" : L"框选后立即完成：已关闭");
            break;
        }
        case ID_TRAY_OPEN_CONFIG:
            ShellExecuteW(hwnd, L"open", GetConfigPath().c_str(),
                          NULL, NULL, SW_SHOWNORMAL);
            break;
        case ID_TRAY_DELAY_CAPTURE:
            StartCapture(hwnd, ReadDelaySecondsConfig());
            break;
        }
        break;
    case WM_DESTROY:
        RemoveClipboardFormatListener(hwnd);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // 优先开启 Per-Monitor DPI V2（Win10 1703+），让每块屏各自用真实 DPI，
    // 避免跨屏截图时因缩放差异被位图拉伸。失败则退回 System DPI Aware。
    typedef BOOL (WINAPI *PFN_SetProcessDpiAwarenessContext)(HANDLE);
    HMODULE hUser = GetModuleHandleW(L"user32.dll");
    auto pSetCtx = hUser ? (PFN_SetProcessDpiAwarenessContext)
        GetProcAddress(hUser, "SetProcessDpiAwarenessContext") : NULL;
    if (!pSetCtx || !pSetCtx((HANDLE)-4 /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */))
        SetProcessDPIAware();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"AdvancedPaste_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"截图工具已在运行中", L"提示", MB_OK);
        return 0;
    }

    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, NULL);

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hIcon = LoadAppIcon(hInstance, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
    wc.hIconSm = LoadAppIcon(hInstance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    wc.lpszClassName = L"AdvancedPasteMain";
    RegisterClassExW(&wc);

    g_hwndMain = CreateWindowExW(0, L"AdvancedPasteMain", L"Advanced Paste",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    AddClipboardFormatListener(g_hwndMain);

    // 读取并解析快捷键配置
    std::wstring hotkeyStr = ReadHotkeyConfig();
    EnsureConfigDefaults(); // 补齐老版本 config.ini 里缺失的 key
    if (!ParseHotkey(hotkeyStr, &g_hkMod, &g_hkVk)) {
        MessageBoxW(NULL, (L"快捷键配置无效: " + hotkeyStr).c_str(),
                    L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 优先用 RegisterHotKey，失败则回退到键盘钩子拦截
    if (!RegisterHotKey(g_hwndMain, ID_HOTKEY, g_hkMod, g_hkVk)) {
        g_hkUseHook = true;
    }

    // 安装键盘钩子
    g_hKeyHook = SetWindowsHookExW(WH_KEYBOARD_LL, LLKeyboardProc, hInstance, 0);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwndMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadAppIcon(hInstance, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
    std::wstring tip = L"截图工具 (" + hotkeyStr + L")";
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    UnhookWindowsHookEx(g_hKeyHook);
    UnregisterHotKey(g_hwndMain, ID_HOTKEY);
    Gdiplus::GdiplusShutdown(gdipToken);
    CoUninitialize();
    CloseHandle(hMutex);
    return 0;
}
