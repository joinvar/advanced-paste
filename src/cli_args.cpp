#include "cli_args.h"
#include <cstdio>
#include <cwctype>
#include <cwchar>

const wchar_t* FindOpt(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 0; i < argc; i++) {
        if (wcscmp(argv[i], name) == 0 && i + 1 < argc)
            return argv[i + 1];
    }
    return NULL;
}

bool HasFlag(int argc, wchar_t** argv, const wchar_t* name) {
    for (int i = 0; i < argc; i++)
        if (wcscmp(argv[i], name) == 0) return true;
    return false;
}

bool ParseRect(const wchar_t* s, int* x, int* y, int* w, int* h) {
    if (!s) return false;
    return swscanf_s(s, L"%d,%d,%d,%d", x, y, w, h) == 4;
}

bool ParseXY(const wchar_t* s, int* x, int* y) {
    if (!s) return false;
    return swscanf_s(s, L"%d,%d", x, y) == 2;
}

bool ParseKeyName(std::wstring name, WORD* vk) {
    for (auto& c : name) c = (wchar_t)towlower(c);
    if (name == L"ctrl" || name == L"control")   { *vk = VK_CONTROL; return true; }
    if (name == L"alt"  || name == L"menu")      { *vk = VK_MENU;    return true; }
    if (name == L"shift")                         { *vk = VK_SHIFT;   return true; }
    if (name == L"win"  || name == L"super")     { *vk = VK_LWIN;    return true; }
    if (name == L"esc"  || name == L"escape")    { *vk = VK_ESCAPE;  return true; }
    if (name == L"enter"|| name == L"return")    { *vk = VK_RETURN;  return true; }
    if (name == L"tab")                           { *vk = VK_TAB;     return true; }
    if (name == L"space")                         { *vk = VK_SPACE;   return true; }
    if (name == L"backspace" || name == L"back") { *vk = VK_BACK;    return true; }
    if (name == L"delete"|| name == L"del")      { *vk = VK_DELETE;  return true; }
    if (name == L"up")    { *vk = VK_UP;    return true; }
    if (name == L"down")  { *vk = VK_DOWN;  return true; }
    if (name == L"left")  { *vk = VK_LEFT;  return true; }
    if (name == L"right") { *vk = VK_RIGHT; return true; }
    if (name == L"home")  { *vk = VK_HOME;  return true; }
    if (name == L"end")   { *vk = VK_END;   return true; }
    if (name == L"pageup")   { *vk = VK_PRIOR; return true; }
    if (name == L"pagedown") { *vk = VK_NEXT;  return true; }
    if (name.size() >= 2 && name[0] == L'f' && iswdigit(name[1])) {
        int n = _wtoi(name.c_str() + 1);
        if (n >= 1 && n <= 24) { *vk = (WORD)(VK_F1 + n - 1); return true; }
    }
    if (name.size() == 1 && name[0] >= L'a' && name[0] <= L'z') { *vk = (WORD)towupper(name[0]); return true; }
    if (name.size() == 1 && name[0] >= L'0' && name[0] <= L'9') { *vk = (WORD)name[0]; return true; }
    return false;
}

bool ParseButton(const wchar_t* s, DWORD* downFlag, DWORD* upFlag) {
    if (!s || wcscmp(s, L"left") == 0)   { *downFlag = MOUSEEVENTF_LEFTDOWN;   *upFlag = MOUSEEVENTF_LEFTUP;   return true; }
    if (wcscmp(s, L"right") == 0)        { *downFlag = MOUSEEVENTF_RIGHTDOWN;  *upFlag = MOUSEEVENTF_RIGHTUP;  return true; }
    if (wcscmp(s, L"middle") == 0)       { *downFlag = MOUSEEVENTF_MIDDLEDOWN; *upFlag = MOUSEEVENTF_MIDDLEUP; return true; }
    return false;
}

std::wstring JsonEscape(const std::wstring& s) {
    std::wstring r;
    r.reserve(s.size() + 2);
    for (wchar_t c : s) {
        if (c == L'"' || c == L'\\') { r += L'\\'; r += c; }
        else if (c == L'\n') r += L"\\n";
        else if (c == L'\r') r += L"\\r";
        else if (c == L'\t') r += L"\\t";
        else if (c < 0x20) {
            wchar_t buf[8];
            swprintf_s(buf, 8, L"\\u%04x", (unsigned)c);
            r += buf;
        } else {
            r += c;
        }
    }
    return r;
}
