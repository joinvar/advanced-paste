#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include "overlay.h"
#include "resource.h"
#include "utils.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

#define WM_TRAYICON  (WM_USER + 1)
#define WM_DO_PASTE  (WM_USER + 2)
#define ID_HOTKEY    1
#define ID_TRAY_EXIT 1001

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
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            PostQuitMessage(0);
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
