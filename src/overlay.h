#pragma once
#include <windows.h>

// 启动截图覆盖层。delaySec > 0 时先倒计时，再抓取鼠标所在屏
void StartCapture(HWND hParent, int delaySec = 0);

// 获取待粘贴的截图位图（未裁剪的已裁剪结果），无则返回 NULL
HBITMAP GetPendingBitmap(int* w, int* h);

// 清除待粘贴位图（保存成功后调用）
void ClearPendingBitmap();

// 剪贴板被外部修改时调用，清除待粘贴位图
// 返回 true 表示确实清除了，false 表示是本程序自己写入的剪贴板
bool OnClipboardChanged();
