#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <string>

// 将 HBITMAP 保存为 PNG 到指定目录，自动编号，返回文件路径
std::wstring SaveBitmapAsPng(HBITMAP hBitmap, int w, int h, const std::wstring& dir);

// 获取当前活动的 Explorer 窗口所在文件夹路径
std::wstring GetActiveExplorerPath();

// 当前前台窗口是否属于 Explorer
bool IsForegroundExplorerWindow();

// 获取 GDI+ PNG 编码器 CLSID
int GetEncoderClsid(const WCHAR* format, CLSID* pClsid);

// 获取 exe 同目录下 config.ini 路径
std::wstring GetConfigPath();

// 读取快捷键配置，不存在则创建默认配置并返回 "Ctrl+Alt+X"
std::wstring ReadHotkeyConfig();

// 读取文件名前缀配置，可为空
std::wstring ReadPatternConfig();

// 读取框选后是否立即完成（AutoFinishOnSelect），默认 false
bool ReadAutoFinishOnSelectConfig();

// 读取自动保存目录 SaveDir，已展开环境变量；返回空表示走手动粘贴流程
std::wstring ReadSaveDirConfig();

// 启动时调用：补齐老版本 config.ini 里缺失的配置项（仅写入缺失的 key，不覆盖已有值）
void EnsureConfigDefaults();

// 写入 SaveDir（空串表示清除）
void WriteSaveDirConfig(const std::wstring& dir);

// 写入 AutoFinishOnSelect
void WriteAutoFinishOnSelectConfig(bool value);

// 弹出系统文件夹选择对话框；取消或失败返回空字符串
std::wstring PickFolderDialog(HWND owner, const std::wstring& initialDir);

// 解析快捷键字符串为 RegisterHotKey 所需的 modifiers 和 vk
bool ParseHotkey(const std::wstring& str, UINT* modifiers, UINT* vk);
