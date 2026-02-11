#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

// 将 HBITMAP 保存为 PNG 到指定目录，自动编号，返回文件路径
std::wstring SaveBitmapAsPng(HBITMAP hBitmap, int w, int h, const std::wstring& dir);

// 获取当前活动的 Explorer 窗口所在文件夹路径
std::wstring GetActiveExplorerPath();

// 获取 GDI+ PNG 编码器 CLSID
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);
