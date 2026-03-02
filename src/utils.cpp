#include "utils.h"
#include <shlobj.h>
#include <exdisp.h>
#include <shobjidl.h>
#include <algorithm>
#include <vector>
#include <cctype>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")

int GetEncoderClsid(const WCHAR* format, CLSID* pClsid) {
    UINT num = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&num, &size);
    if (size == 0) return -1;
    auto pInfo = (Gdiplus::ImageCodecInfo*)(malloc(size));
    if (!pInfo) return -1;
    Gdiplus::GetImageEncoders(num, size, pInfo);
    for (UINT j = 0; j < num; ++j) {
        if (wcscmp(pInfo[j].MimeType, format) == 0) {
            *pClsid = pInfo[j].Clsid;
            free(pInfo);
            return j;
        }
    }
    free(pInfo);
    return -1;
}

static void ReplaceAll(std::wstring& s, const std::wstring& from, const std::wstring& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::wstring::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
}

static std::wstring ExpandPattern(const std::wstring& pattern) {
    std::wstring result = pattern;
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf_s(buf, L"%04d%02d%02d", st.wYear, st.wMonth, st.wDay);
    ReplaceAll(result, L"{Date}", std::wstring(buf));
    swprintf_s(buf, L"%02d%02d%02d", st.wHour, st.wMinute, st.wSecond);
    ReplaceAll(result, L"{Time}", std::wstring(buf));
    return result;
}

std::wstring SaveBitmapAsPng(HBITMAP hBitmap, int w, int h, const std::wstring& dir) {
    if (!hBitmap || w <= 0 || h <= 0) return L"";

    // 在目标目录中查找下一个可用编号
    std::wstring base = dir;
    if (base.back() != L'\\') base += L'\\';
    std::wstring prefix = ExpandPattern(ReadPatternConfig());
    int num = 1;
    while (GetFileAttributesW((base + prefix + std::to_wstring(num) + L".png").c_str()) != INVALID_FILE_ATTRIBUTES)
        num++;
    std::wstring filePath = base + prefix + std::to_wstring(num) + L".png";

    Gdiplus::Bitmap bmp(hBitmap, NULL);
    CLSID clsid;
    if (GetEncoderClsid(L"image/png", &clsid) >= 0)
        bmp.Save(filePath.c_str(), &clsid, NULL);
    return filePath;
}

std::wstring GetActiveExplorerPath() {
    HWND hwndFg = GetForegroundWindow();
    if (!hwndFg) return L"";

    // 确认前台窗口是 Explorer
    wchar_t cls[64] = {};
    GetClassNameW(hwndFg, cls, 64);
    if (wcscmp(cls, L"CabinetWClass") != 0) return L"";

    IShellWindows* psw = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL,
               IID_IShellWindows, (void**)&psw)) || !psw)
        return L"";

    std::wstring result;
    long count = 0;
    psw->get_Count(&count);

    for (long i = 0; i < count; i++) {
        VARIANT v;
        V_VT(&v) = VT_I4;
        V_I4(&v) = i;
        IDispatch* pDisp = NULL;
        psw->Item(v, &pDisp);
        if (!pDisp) continue;

        IWebBrowserApp* pBrowser = NULL;
        pDisp->QueryInterface(IID_IWebBrowserApp, (void**)&pBrowser);
        pDisp->Release();
        if (!pBrowser) continue;

        SHANDLE_PTR hWnd = 0;
        pBrowser->get_HWND(&hWnd);
        if ((HWND)hWnd != hwndFg) { pBrowser->Release(); continue; }

        IServiceProvider* pSP = NULL;
        pBrowser->QueryInterface(IID_IServiceProvider, (void**)&pSP);
        pBrowser->Release();
        if (!pSP) break;

        IShellBrowser* pSB = NULL;
        pSP->QueryService(SID_STopLevelBrowser, IID_IShellBrowser, (void**)&pSB);
        pSP->Release();
        if (!pSB) break;

        IShellView* pSV = NULL;
        pSB->QueryActiveShellView(&pSV);
        pSB->Release();
        if (!pSV) break;

        IFolderView* pFV = NULL;
        pSV->QueryInterface(IID_IFolderView, (void**)&pFV);
        pSV->Release();
        if (!pFV) break;

        IPersistFolder2* pPF = NULL;
        pFV->GetFolder(IID_IPersistFolder2, (void**)&pPF);
        pFV->Release();
        if (!pPF) break;

        PIDLIST_ABSOLUTE pidl = NULL;
        pPF->GetCurFolder(&pidl);
        pPF->Release();
        if (pidl) {
            wchar_t path[MAX_PATH];
            if (SHGetPathFromIDListW(pidl, path))
                result = path;
            CoTaskMemFree(pidl);
        }
        break;
    }
    psw->Release();
    return result;
}

std::wstring GetConfigPath() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring path(exePath);
    auto pos = path.find_last_of(L'\\');
    if (pos != std::wstring::npos)
        path = path.substr(0, pos + 1);
    return path + L"config.ini";
}

std::wstring ReadHotkeyConfig() {
    std::wstring cfgPath = GetConfigPath();
    const std::wstring defaultHotkey = L"Ctrl+Alt+X";

    wchar_t buf[128] = {};
    GetPrivateProfileStringW(L"Settings", L"Hotkey", L"",
                             buf, 128, cfgPath.c_str());
    std::wstring val(buf);

    if (val.empty()) {
        // 配置不存在，创建默认配置
        WritePrivateProfileStringW(L"Settings", L"Hotkey",
                                   defaultHotkey.c_str(), cfgPath.c_str());
        // 同时写入 Pattern 默认值
        WritePrivateProfileStringW(L"Settings", L"Pattern",
                                   L"", cfgPath.c_str());
        // 同时写入 AutoFinishOnSelect 默认值
        WritePrivateProfileStringW(L"Settings", L"AutoFinishOnSelect",
                                   L"0", cfgPath.c_str());
        return defaultHotkey;
    }
    return val;
}

std::wstring ReadPatternConfig() {
    std::wstring cfgPath = GetConfigPath();
    wchar_t buf[128] = {};
    GetPrivateProfileStringW(L"Settings", L"Pattern", L"",
                             buf, 128, cfgPath.c_str());
    return std::wstring(buf);
}

bool ReadAutoFinishOnSelectConfig() {
    std::wstring cfgPath = GetConfigPath();
    wchar_t buf[32] = {};
    GetPrivateProfileStringW(L"Settings", L"AutoFinishOnSelect", L"",
                             buf, 32, cfgPath.c_str());
    std::wstring val(buf);

    if (val.empty()) {
        WritePrivateProfileStringW(L"Settings", L"AutoFinishOnSelect",
                                   L"0", cfgPath.c_str());
        return false;
    }

    return val == L"1" || _wcsicmp(val.c_str(), L"true") == 0;
}

static std::wstring ToUpper(const std::wstring& s) {
    std::wstring r = s;
    for (auto& c : r) c = towupper(c);
    return r;
}

bool ParseHotkey(const std::wstring& str, UINT* modifiers, UINT* vk) {
    *modifiers = 0;
    *vk = 0;

    // 按 '+' 分割
    std::vector<std::wstring> parts;
    std::wstring cur;
    for (auto c : str) {
        if (c == L'+') {
            if (!cur.empty()) { parts.push_back(cur); cur.clear(); }
        } else if (c != L' ') {
            cur += c;
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    if (parts.empty()) return false;

    // 最后一个是主键，前面都是修饰键
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        std::wstring mod = ToUpper(parts[i]);
        if (mod == L"CTRL" || mod == L"CONTROL") *modifiers |= MOD_CONTROL;
        else if (mod == L"ALT")                  *modifiers |= MOD_ALT;
        else if (mod == L"SHIFT")                *modifiers |= MOD_SHIFT;
        else if (mod == L"WIN")                  *modifiers |= MOD_WIN;
        else return false;
    }

    // 解析主键
    std::wstring key = ToUpper(parts.back());
    if (key.size() == 1 && ((key[0] >= L'A' && key[0] <= L'Z') ||
                            (key[0] >= L'0' && key[0] <= L'9'))) {
        *vk = (UINT)key[0];
    } else if (key.size() >= 2 && key[0] == L'F' &&
               key[1] >= L'1' && key[1] <= L'9') {
        int fnum = _wtoi(key.c_str() + 1);
        if (fnum >= 1 && fnum <= 24) *vk = VK_F1 + fnum - 1;
        else return false;
    } else {
        return false;
    }

    return *vk != 0;
}
