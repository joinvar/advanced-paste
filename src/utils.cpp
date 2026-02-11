#include "utils.h"
#include <shlobj.h>
#include <exdisp.h>
#include <shobjidl.h>
#include <algorithm>

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

std::wstring SaveBitmapAsPng(HBITMAP hBitmap, int w, int h, const std::wstring& dir) {
    if (!hBitmap || w <= 0 || h <= 0) return L"";

    // 在目标目录中查找下一个可用编号
    std::wstring base = dir;
    if (base.back() != L'\\') base += L'\\';
    int num = 1;
    while (GetFileAttributesW((base + std::to_wstring(num) + L".png").c_str()) != INVALID_FILE_ATTRIBUTES)
        num++;
    std::wstring filePath = base + std::to_wstring(num) + L".png";

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
