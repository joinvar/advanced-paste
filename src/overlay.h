#pragma once
#include <windows.h>

// 启动截图覆盖层
void StartCapture(HWND hParent);

// 获取待粘贴的截图位图（未裁剪的已裁剪结果），无则返回 NULL
HBITMAP GetPendingBitmap(int* w, int* h);

// 清除待粘贴位图（保存成功后调用）
void ClearPendingBitmap();
