#include "render.h"
#include <algorithm>
#include <cmath>

#pragma comment(lib, "msimg32.lib")

void DrawGradientBorder(HDC hdc, const RECT& sel, COLORREF color,
                        int borderW, BYTE maxAlpha) {
    int cr = GetRValue(color), cg = GetGValue(color), cb = GetBValue(color);
    int selW = sel.right - sel.left;
    int selH = sel.bottom - sel.top;
    if (selW <= 0 || selH <= 0 || borderW <= 0) return;
    borderW = (std::min)(borderW, (std::min)(selW, selH) / 2);
    if (borderW <= 0) return;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = selW;
    bmi.bmiHeader.biHeight = -selH;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    BYTE* bits = nullptr;
    HBITMAP hBmp = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS,
                                    (void**)&bits, NULL, 0);
    if (!hBmp || !bits) return;
    memset(bits, 0, (size_t)selW * selH * 4);

    for (int row = 0; row < selH; row++) {
        int dY = (std::min)(row, selH - 1 - row);
        for (int col = 0; col < selW; col++) {
            int dX = (std::min)(col, selW - 1 - col);
            int d = (std::min)(dY, dX);
            if (d >= borderW) continue;
            double t = (double)d / borderW;
            int a = (int)(maxAlpha * (1.0 + cos(t * 3.14159265)) / 2.0);
            int idx = (row * selW + col) * 4;
            bits[idx]     = (BYTE)(cb * a / 255);
            bits[idx + 1] = (BYTE)(cg * a / 255);
            bits[idx + 2] = (BYTE)(cr * a / 255);
            bits[idx + 3] = (BYTE)a;
        }
    }

    HDC hdcBmp = CreateCompatibleDC(hdc);
    HBITMAP hOld = (HBITMAP)SelectObject(hdcBmp, hBmp);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    AlphaBlend(hdc, sel.left, sel.top, selW, selH,
               hdcBmp, 0, 0, selW, selH, bf);
    SelectObject(hdcBmp, hOld);
    DeleteDC(hdcBmp);
    DeleteObject(hBmp);
}
