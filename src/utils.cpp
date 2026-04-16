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

static HWND GetRootWindowOrSelf(HWND hwnd) {
    if (!hwnd) return NULL;
    HWND root = GetAncestor(hwnd, GA_ROOT);
    return root ? root : hwnd;
}

static bool IsExplorerFrameWindow(HWND hwnd) {
    if (!hwnd) return false;
    wchar_t cls[64] = {};
    if (!GetClassNameW(hwnd, cls, 64))
        return false;
    return wcscmp(cls, L"CabinetWClass") == 0 || wcscmp(cls, L"ExploreWClass") == 0;
}

static HWND GetForegroundFocusWindow() {
    HWND hwndFg = GetForegroundWindow();
    if (!hwndFg) return NULL;

    DWORD threadId = GetWindowThreadProcessId(hwndFg, NULL);
    if (!threadId) return hwndFg;

    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(gti);
    if (!GetGUIThreadInfo(threadId, &gti))
        return hwndFg;

    if (gti.hwndFocus) return gti.hwndFocus;
    if (gti.hwndActive) return gti.hwndActive;
    return hwndFg;
}

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
    if (!hBitmap || w <= 0 || h <= 0 || dir.empty()) return L"";

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
    if (GetEncoderClsid(L"image/png", &clsid) < 0) return L"";
    if (bmp.Save(filePath.c_str(), &clsid, NULL) != Gdiplus::Ok) return L"";
    return filePath;
}

std::wstring GetActiveExplorerPath() {
    HWND hwndFg = GetForegroundWindow();
    if (!hwndFg) return L"";
    HWND hwndRoot = GetRootWindowOrSelf(hwndFg);
    HWND hwndFocus = GetForegroundFocusWindow();

    // 确认前台窗口是 Explorer
    if (!IsExplorerFrameWindow(hwndRoot)) return L"";

    IShellWindows* psw = NULL;
    if (FAILED(CoCreateInstance(CLSID_ShellWindows, NULL, CLSCTX_ALL,
               IID_IShellWindows, (void**)&psw)) || !psw)
        return L"";

    std::wstring result;
    std::wstring fallbackResult;
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
        if ((HWND)hWnd != hwndRoot) { pBrowser->Release(); continue; }

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

        HWND hwndView = NULL;
        pSV->GetWindow(&hwndView);

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
            if (SHGetPathFromIDListW(pidl, path)) {
                if (fallbackResult.empty())
                    fallbackResult = path;

                if (hwndFocus && hwndView &&
                    (hwndFocus == hwndView || IsChild(hwndView, hwndFocus))) {
                    result = path;
                    CoTaskMemFree(pidl);
                    break;
                }
            }
            CoTaskMemFree(pidl);
        }
    }
    psw->Release();
    return !result.empty() ? result : fallbackResult;
}

bool IsForegroundExplorerWindow() {
    return IsExplorerFrameWindow(GetRootWindowOrSelf(GetForegroundWindow()));
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
        // 同时写入 SaveDir 默认值（空）
        WritePrivateProfileStringW(L"Settings", L"SaveDir",
                                   L"", cfgPath.c_str());
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

void EnsureConfigDefaults() {
    std::wstring cfgPath = GetConfigPath();
    // 用一个不可能作为正常取值的哨兵，区分 "key 不存在" 与 "key 存在但为空"
    const wchar_t* SENTINEL = L"\x01__ABSENT__\x01";
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Settings", L"SaveDir", SENTINEL,
                             buf, MAX_PATH, cfgPath.c_str());
    if (wcscmp(buf, SENTINEL) == 0)
        WritePrivateProfileStringW(L"Settings", L"SaveDir", L"", cfgPath.c_str());

    GetPrivateProfileStringW(L"Settings", L"DelaySeconds", SENTINEL,
                             buf, MAX_PATH, cfgPath.c_str());
    if (wcscmp(buf, SENTINEL) == 0)
        WritePrivateProfileStringW(L"Settings", L"DelaySeconds", L"3", cfgPath.c_str());
}

int ReadDelaySecondsConfig() {
    std::wstring cfgPath = GetConfigPath();
    int v = GetPrivateProfileIntW(L"Settings", L"DelaySeconds", 3, cfgPath.c_str());
    if (v < 1)  v = 1;
    if (v > 60) v = 60;
    return v;
}

void WriteSaveDirConfig(const std::wstring& dir) {
    WritePrivateProfileStringW(L"Settings", L"SaveDir", dir.c_str(),
                               GetConfigPath().c_str());
}

void WriteAutoFinishOnSelectConfig(bool value) {
    WritePrivateProfileStringW(L"Settings", L"AutoFinishOnSelect",
                               value ? L"1" : L"0", GetConfigPath().c_str());
}

std::wstring PickFolderDialog(HWND owner, const std::wstring& initialDir) {
    IFileDialog* pfd = NULL;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL,
                                IID_PPV_ARGS(&pfd))) || !pfd)
        return L"";

    std::wstring result;
    DWORD opts = 0;
    pfd->GetOptions(&opts);
    pfd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    pfd->SetTitle(L"选择截图保存目录");

    if (!initialDir.empty()) {
        IShellItem* psi = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(initialDir.c_str(), NULL,
                                                  IID_PPV_ARGS(&psi))) && psi) {
            pfd->SetFolder(psi);
            psi->Release();
        }
    }

    if (SUCCEEDED(pfd->Show(owner))) {
        IShellItem* psi = NULL;
        if (SUCCEEDED(pfd->GetResult(&psi)) && psi) {
            PWSTR path = NULL;
            if (SUCCEEDED(psi->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                result = path;
                CoTaskMemFree(path);
            }
            psi->Release();
        }
    }
    pfd->Release();
    return result;
}

std::wstring ReadSaveDirConfig() {
    std::wstring cfgPath = GetConfigPath();
    wchar_t buf[MAX_PATH] = {};
    GetPrivateProfileStringW(L"Settings", L"SaveDir", L"",
                             buf, MAX_PATH, cfgPath.c_str());
    std::wstring val(buf);
    if (val.empty()) return L"";

    // 展开环境变量，如 %USERPROFILE%\Pictures
    wchar_t expanded[MAX_PATH] = {};
    DWORD n = ExpandEnvironmentStringsW(val.c_str(), expanded, MAX_PATH);
    if (n > 0 && n <= MAX_PATH) val = expanded;

    // 去掉末尾的反斜杠和空格，统一由 SaveBitmapAsPng 处理分隔符
    while (!val.empty() && (val.back() == L' ' || val.back() == L'\\' || val.back() == L'/'))
        val.pop_back();
    return val;
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
