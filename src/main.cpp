#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include "overlay.h"
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

// 低级键盘钩子：拦截 Explorer 中的 Ctrl+V
static LRESULT CALLBACK LLKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && wParam == WM_KEYDOWN) {
        auto* kb = (KBDLLHOOKSTRUCT*)lParam;
        if (kb->vkCode == 'V' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (GetPendingBitmap(NULL, NULL)) {
                HWND hwndFg = GetForegroundWindow();
                wchar_t cls[64] = {};
                GetClassNameW(hwndFg, cls, 64);
                if (wcscmp(cls, L"CabinetWClass") == 0) {
                    PostMessageW(g_hwndMain, WM_DO_PASTE, 0, 0);
                    return 1; // 吃掉按键
                }
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
    wc.lpszClassName = L"AdvancedPasteMain";
    RegisterClassExW(&wc);

    g_hwndMain = CreateWindowExW(0, L"AdvancedPasteMain", L"Advanced Paste",
        0, 0, 0, 0, 0, HWND_MESSAGE, NULL, hInstance, NULL);

    if (!RegisterHotKey(g_hwndMain, ID_HOTKEY, MOD_CONTROL | MOD_ALT, 'X')) {
        MessageBoxW(NULL, L"无法注册热键 Ctrl+Alt+X", L"错误", MB_OK | MB_ICONERROR);
        return 1;
    }

    // 安装键盘钩子
    g_hKeyHook = SetWindowsHookExW(WH_KEYBOARD_LL, LLKeyboardProc, hInstance, 0);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwndMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy_s(g_nid.szTip, L"截图工具 (Ctrl+Alt+X)");
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
