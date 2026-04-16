#pragma once
#include <windows.h>

// 在 HDC 上绘制"从边缘向内渐淡"的发光边框。
// 画在 sel 指定的矩形上，color 为边框基色，
// borderW 是边框厚度（像素），maxAlpha 是最外侧的不透明度（0..255）。
// 不透明度用 (1+cos(pi*t))/2 沿距离最近边的法向从 maxAlpha 衰减到 0。
void DrawGradientBorder(HDC hdc, const RECT& sel, COLORREF color,
                        int borderW, BYTE maxAlpha);
