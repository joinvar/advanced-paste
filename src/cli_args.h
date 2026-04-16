#pragma once
#include <windows.h>
#include <string>

// CLI 通用参数解析与输出格式化工具。抽出来是为了在 GTest 里能单独覆盖，
// cli.cpp 自己也 include 本头并去掉对应的 static 实现。

// ===== 参数查找 =====
// 在 argv 里找 --name 的值（紧跟的下一个参数）；找不到返回 NULL
const wchar_t* FindOpt(int argc, wchar_t** argv, const wchar_t* name);

// 判断 argv 里是否出现了 --flag（只判断存在性，不取值）
bool HasFlag(int argc, wchar_t** argv, const wchar_t* name);

// ===== 值解析 =====
// 解析 "X,Y,W,H" 四元组，全部成功返回 true
bool ParseRect(const wchar_t* s, int* x, int* y, int* w, int* h);

// 解析 "X,Y" 二元组
bool ParseXY(const wchar_t* s, int* x, int* y);

// 把键名（大小写不敏感）转为 Windows VK 码。
// 支持：ctrl/control, alt/menu, shift, win/super, esc/escape,
//       enter/return, tab, space, backspace/back, delete/del,
//       up/down/left/right, home/end, pageup/pagedown,
//       F1..F24, A..Z, 0..9
bool ParseKeyName(std::wstring name, WORD* vk);

// 解析鼠标按钮名："left" / "right" / "middle"，输出对应的 MOUSEEVENTF 标志位
bool ParseButton(const wchar_t* s, DWORD* downFlag, DWORD* upFlag);

// ===== 格式化 =====
// JSON 字符串转义（双引号/反斜杠/\n\r\t/0x00-0x1F），不加外层引号
std::wstring JsonEscape(const std::wstring& s);
