#include "overlay.h"
#include "utils.h"
#include <dwmapi.h>
#include <shlobj.h>
#include <algorithm>
#include <vector>
#include <cmath>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "msimg32.lib")

// --- 类型定义 ---
struct WndRect { HWND hwnd; RECT rc; };
enum OvState { ST_SELECT, ST_EDIT };
enum Tool    { TL_NONE, TL_ARROW, TL_RECT, TL_BRUSH };
struct Anno  { Tool type; POINT p1, p2; std::vector<POINT> pts; };

struct TBtn {
    RECT rc;          // overlay 坐标
    const wchar_t* text;
    int id;           // 0=箭头, 1=矩形, 2=画笔, 3=完成, 4=取消
};

// --- 全局状态 ---
static HBITMAP  g_hScreenBmp = NULL;
static int      g_scrX, g_scrY, g_scrW, g_scrH;
static HWND     g_hOverlay = NULL;
static HWND     g_hParent = NULL;
static std::vector<WndRect> g_wnds;

// 选区阶段
static OvState  g_state = ST_SELECT;
static BOOL     g_dragging = FALSE;
static POINT    g_ptStart = {}, g_ptEnd = {};
static RECT     g_rcHighlight = {};

// 编辑阶段
static RECT     g_selRect = {};       // 选区（屏幕坐标）
static Tool     g_curTool = TL_NONE;
static bool     g_drawing = false;
static POINT    g_drawP1 = {}, g_drawP2 = {};
static std::vector<POINT> g_curBrushPts;   // 画笔当前笔画（屏幕坐标）
static std::vector<Anno> g_annos;
static const int BTN_COUNT = 5;
static TBtn     g_btns[BTN_COUNT];
static const int BTN_W = 56, BTN_H = 28, BTN_GAP = 4;

// 待粘贴位图
static HBITMAP  g_pendingBmp = NULL;
static int      g_pendingW = 0, g_pendingH = 0;
static bool     g_selfClipboard = false; // 标记本程序正在写入剪贴板

// 目标屏闪烁边框
static bool     g_borderFlashOn = true;
#define OVERLAY_FLASH_ID 1

HBITMAP GetPendingBitmap(int* w, int* h) {
    if (w) *w = g_pendingW;
    if (h) *h = g_pendingH;
    return g_pendingBmp;
}

void ClearPendingBitmap() {
    if (g_pendingBmp) {
        DeleteObject(g_pendingBmp);
        g_pendingBmp = NULL;
    }
    g_pendingW = 0;
    g_pendingH = 0;
}

bool OnClipboardChanged() {
    if (g_selfClipboard) {
        g_selfClipboard = false;
        return false;
    }
    if (g_pendingBmp) {
        ClearPendingBitmap();
        return true;
    }
    return false;
}

// --- 辅助函数 ---
static void GetWindowRectDwm(HWND hwnd, RECT* rc) {
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, rc, sizeof(RECT))))
        GetWindowRect(hwnd, rc);
}

static BOOL CALLBACK EnumWndProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    RECT rc;
    GetWindowRectDwm(hwnd, &rc);
    if (rc.right > rc.left && rc.bottom > rc.top)
        g_wnds.push_back({ hwnd, rc });
    return TRUE;
}

static RECT FindWindowRectAtPoint(POINT pt) {
    for (auto& w : g_wnds)
        if (PtInRect(&w.rc, pt)) return w.rc;
    return {};
}

// 初始化工具栏按钮位置（选区下方居中）
static void InitToolbar() {
    int totalW = BTN_COUNT * BTN_W + (BTN_COUNT - 1) * BTN_GAP;
    // overlay 坐标中的选区
    int selL = g_selRect.left - g_scrX, selR = g_selRect.right - g_scrX;
    int selB = g_selRect.bottom - g_scrY;
    int startX = selL + (selR - selL - totalW) / 2;
    int y = selB + 8;
    // 如果下方空间不够，放到上方
    if (y + BTN_H > g_scrH)
        y = (g_selRect.top - g_scrY) - BTN_H - 8;

    const wchar_t* labels[] = { L"箭头", L"矩形", L"画笔", L"完成", L"取消" };
    for (int i = 0; i < BTN_COUNT; i++) {
        g_btns[i].rc = { startX + i * (BTN_W + BTN_GAP), y,
                         startX + i * (BTN_W + BTN_GAP) + BTN_W, y + BTN_H };
        g_btns[i].text = labels[i];
        g_btns[i].id = i;
    }
}

// 在 HDC 上绘制一个箭头（overlay 坐标）
static void DrawArrow(HDC hdc, POINT from, POINT to) {
    HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 40, 40));
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hBr = CreateSolidBrush(RGB(255, 40, 40));
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);

    MoveToEx(hdc, from.x, from.y, NULL);
    LineTo(hdc, to.x, to.y);

    double angle = atan2((double)(to.y - from.y), (double)(to.x - from.x));
    double aLen = 16.0, aAng = 0.45;
    POINT tri[3] = {
        to,
        { (LONG)(to.x - aLen * cos(angle - aAng)), (LONG)(to.y - aLen * sin(angle - aAng)) },
        { (LONG)(to.x - aLen * cos(angle + aAng)), (LONG)(to.y - aLen * sin(angle + aAng)) }
    };
    Polygon(hdc, tri, 3);

    SelectObject(hdc, hOldBr); DeleteObject(hBr);
    SelectObject(hdc, hOld);   DeleteObject(hPen);
}

// 在 HDC 上绘制矩形标注（overlay 坐标）
static void DrawAnnoRect(HDC hdc, POINT p1, POINT p2) {
    HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 40, 40));
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, (std::min)(p1.x, p2.x), (std::min)(p1.y, p2.y),
                   (std::max)(p1.x, p2.x), (std::max)(p1.y, p2.y));
    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOld); DeleteObject(hPen);
}

// 在 HDC 上绘制画笔笔画（pts 已转到目标 DC 坐标）
static void DrawBrush(HDC hdc, const std::vector<POINT>& pts) {
    if (pts.size() < 2) {
        if (pts.size() == 1) {
            // 单点：画一个小圆点
            HBRUSH hBr = CreateSolidBrush(RGB(255, 40, 40));
            HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
            HPEN hPen = CreatePen(PS_SOLID, 1, RGB(255, 40, 40));
            HPEN hOld = (HPEN)SelectObject(hdc, hPen);
            Ellipse(hdc, pts[0].x - 2, pts[0].y - 2, pts[0].x + 2, pts[0].y + 2);
            SelectObject(hdc, hOld); DeleteObject(hPen);
            SelectObject(hdc, hOldBr); DeleteObject(hBr);
        }
        return;
    }
    HPEN hPen = CreatePen(PS_SOLID, 3, RGB(255, 40, 40));
    HPEN hOld = (HPEN)SelectObject(hdc, hPen);
    // 让线端更圆润
    Polyline(hdc, pts.data(), (int)pts.size());
    SelectObject(hdc, hOld); DeleteObject(hPen);
}

// 绘制所有标注（需传入偏移量，将屏幕坐标转为目标 DC 坐标）
static void DrawAnnotations(HDC hdc, int offX, int offY) {
    for (auto& a : g_annos) {
        if (a.type == TL_BRUSH) {
            std::vector<POINT> pts;
            pts.reserve(a.pts.size());
            for (auto& p : a.pts) pts.push_back({ p.x + offX, p.y + offY });
            DrawBrush(hdc, pts);
        } else {
            POINT p1 = { a.p1.x + offX, a.p1.y + offY };
            POINT p2 = { a.p2.x + offX, a.p2.y + offY };
            if (a.type == TL_ARROW) DrawArrow(hdc, p1, p2);
            else if (a.type == TL_RECT) DrawAnnoRect(hdc, p1, p2);
        }
    }
    // 正在绘制的标注
    if (g_drawing && g_curTool != TL_NONE) {
        if (g_curTool == TL_BRUSH) {
            std::vector<POINT> pts;
            pts.reserve(g_curBrushPts.size());
            for (auto& p : g_curBrushPts) pts.push_back({ p.x + offX, p.y + offY });
            DrawBrush(hdc, pts);
        } else {
            POINT p1 = { g_drawP1.x + offX, g_drawP1.y + offY };
            POINT p2 = { g_drawP2.x + offX, g_drawP2.y + offY };
            if (g_curTool == TL_ARROW) DrawArrow(hdc, p1, p2);
            else DrawAnnoRect(hdc, p1, p2);
        }
    }
}

// 完成截图：裁剪 + 合成标注 → 存为待粘贴位图
static void FinishCapture() {
    RECT region = {
        g_selRect.left - g_scrX, g_selRect.top - g_scrY,
        g_selRect.right - g_scrX, g_selRect.bottom - g_scrY
    };
    region.left   = (std::max)(0L, region.left);
    region.top    = (std::max)(0L, region.top);
    region.right  = (std::min)((LONG)g_scrW, region.right);
    region.bottom = (std::min)((LONG)g_scrH, region.bottom);

    int w = (int)(region.right - region.left);
    int h = (int)(region.bottom - region.top);
    if (w <= 0 || h <= 0) return;

    HDC hdcScr = GetDC(NULL);
    HDC hdcSrc = CreateCompatibleDC(hdcScr);
    HDC hdcDst = CreateCompatibleDC(hdcScr);
    HBITMAP hbmCrop = CreateCompatibleBitmap(hdcScr, w, h);
    SelectObject(hdcSrc, g_hScreenBmp);
    SelectObject(hdcDst, hbmCrop);
    BitBlt(hdcDst, 0, 0, w, h, hdcSrc, region.left, region.top, SRCCOPY);
    DeleteDC(hdcSrc);

    // 在裁剪位图上绘制标注（屏幕坐标 → 位图坐标）
    DrawAnnotations(hdcDst, -g_selRect.left, -g_selRect.top);

    DeleteDC(hdcDst);
    ReleaseDC(NULL, hdcScr);

    if (g_pendingBmp) DeleteObject(g_pendingBmp);
    g_pendingBmp = hbmCrop;
    g_pendingW = w;
    g_pendingH = h;

    // 如配置了 SaveDir，直接按 Pattern 命名保存一份到该目录
    std::wstring saveDir = ReadSaveDirConfig();
    std::wstring savedPath;
    if (!saveDir.empty()) {
        SHCreateDirectoryExW(NULL, saveDir.c_str(), NULL); // 不存在则创建
        savedPath = SaveBitmapAsPng(g_pendingBmp, w, h, saveDir);
    }

    // 写入系统剪贴板：CF_BITMAP 给图像类应用；若已自动保存成功，
    // 额外放一份 CF_UNICODETEXT(绝对路径) 给终端/命令行直接 Ctrl+V 使用
    if (OpenClipboard(g_hOverlay)) {
        g_selfClipboard = true;
        EmptyClipboard();
        HDC hdcScr2 = GetDC(NULL);
        HDC hdcSrc2 = CreateCompatibleDC(hdcScr2);
        HDC hdcDst2 = CreateCompatibleDC(hdcScr2);
        HBITMAP hCopy = CreateCompatibleBitmap(hdcScr2, w, h);
        SelectObject(hdcSrc2, g_pendingBmp);
        SelectObject(hdcDst2, hCopy);
        BitBlt(hdcDst2, 0, 0, w, h, hdcSrc2, 0, 0, SRCCOPY);
        DeleteDC(hdcSrc2);
        DeleteDC(hdcDst2);
        ReleaseDC(NULL, hdcScr2);
        SetClipboardData(CF_BITMAP, hCopy);

        if (!savedPath.empty()) {
            size_t bytes = (savedPath.size() + 1) * sizeof(wchar_t);
            HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (hMem) {
                void* p = GlobalLock(hMem);
                memcpy(p, savedPath.c_str(), bytes);
                GlobalUnlock(hMem);
                SetClipboardData(CF_UNICODETEXT, hMem);
            }
        }
        CloseClipboard();
    }
}

// --- 绘制 ---
static void PaintOverlay(HWND, HDC hdc) {
    HDC hdcBack = CreateCompatibleDC(hdc);
    HBITMAP hbmBack = CreateCompatibleBitmap(hdc, g_scrW, g_scrH);
    HBITMAP hbmOldBack = (HBITMAP)SelectObject(hdcBack, hbmBack);

    // 底图
    HDC hdcSrc = CreateCompatibleDC(hdc);
    HBITMAP hbmOldSrc = (HBITMAP)SelectObject(hdcSrc, g_hScreenBmp);
    BitBlt(hdcBack, 0, 0, g_scrW, g_scrH, hdcSrc, 0, 0, SRCCOPY);

    // 半透明遮罩
    HDC hdcDim = CreateCompatibleDC(hdc);
    HBITMAP hbmDim = CreateCompatibleBitmap(hdc, g_scrW, g_scrH);
    HBITMAP hbmOldDim = (HBITMAP)SelectObject(hdcDim, hbmDim);
    HBRUSH hBrB = CreateSolidBrush(RGB(0, 0, 0));
    RECT rcF = { 0, 0, g_scrW, g_scrH };
    FillRect(hdcDim, &rcF, hBrB);
    DeleteObject(hBrB);
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 100, 0 };
    AlphaBlend(hdcBack, 0, 0, g_scrW, g_scrH, hdcDim, 0, 0, g_scrW, g_scrH, bf);
    SelectObject(hdcDim, hbmOldDim);
    DeleteObject(hbmDim);
    DeleteDC(hdcDim);

    // 选区高亮
    RECT sel;
    if (g_state == ST_SELECT) {
        if (g_dragging) {
            sel = { (std::min)(g_ptStart.x, g_ptEnd.x) - g_scrX,
                    (std::min)(g_ptStart.y, g_ptEnd.y) - g_scrY,
                    (std::max)(g_ptStart.x, g_ptEnd.x) - g_scrX,
                    (std::max)(g_ptStart.y, g_ptEnd.y) - g_scrY };
        } else {
            sel = g_rcHighlight;
            OffsetRect(&sel, -g_scrX, -g_scrY);
        }
    } else {
        sel = g_selRect;
        OffsetRect(&sel, -g_scrX, -g_scrY);
    }
    sel.left = (std::max)(0L, sel.left);   sel.top = (std::max)(0L, sel.top);
    sel.right = (std::min)((LONG)g_scrW, sel.right);
    sel.bottom = (std::min)((LONG)g_scrH, sel.bottom);

    int sw = sel.right - sel.left, sh = sel.bottom - sel.top;
    if (sw > 0 && sh > 0) {
        BitBlt(hdcBack, sel.left, sel.top, sw, sh, hdcSrc, sel.left, sel.top, SRCCOPY);
        HPEN hPen = CreatePen(PS_SOLID, 2, RGB(0, 174, 255));
        HPEN hOP = (HPEN)SelectObject(hdcBack, hPen);
        HBRUSH hOB = (HBRUSH)SelectObject(hdcBack, GetStockObject(NULL_BRUSH));
        Rectangle(hdcBack, sel.left, sel.top, sel.right, sel.bottom);
        SelectObject(hdcBack, hOP); SelectObject(hdcBack, hOB);
        DeleteObject(hPen);
    }

    // 编辑模式：绘制标注 + 工具栏
    if (g_state == ST_EDIT) {
        DrawAnnotations(hdcBack, -g_scrX, -g_scrY);

        // 工具栏背景
        SetBkMode(hdcBack, TRANSPARENT);
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
        HFONT hOldFont = (HFONT)SelectObject(hdcBack, hFont);

        for (int i = 0; i < BTN_COUNT; i++) {
            bool active = (i == 0 && g_curTool == TL_ARROW) ||
                          (i == 1 && g_curTool == TL_RECT) ||
                          (i == 2 && g_curTool == TL_BRUSH);
            COLORREF bgCol = active ? RGB(0, 120, 215) : RGB(50, 50, 50);
            HBRUSH hBr = CreateSolidBrush(bgCol);
            FillRect(hdcBack, &g_btns[i].rc, hBr);
            DeleteObject(hBr);
            // 边框
            HPEN hBP = CreatePen(PS_SOLID, 1, RGB(120, 120, 120));
            HPEN hBPO = (HPEN)SelectObject(hdcBack, hBP);
            HBRUSH hBBO = (HBRUSH)SelectObject(hdcBack, GetStockObject(NULL_BRUSH));
            Rectangle(hdcBack, g_btns[i].rc.left, g_btns[i].rc.top,
                      g_btns[i].rc.right, g_btns[i].rc.bottom);
            SelectObject(hdcBack, hBPO); SelectObject(hdcBack, hBBO);
            DeleteObject(hBP);
            // 文字
            SetTextColor(hdcBack, RGB(255, 255, 255));
            DrawTextW(hdcBack, g_btns[i].text, -1, &g_btns[i].rc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        SelectObject(hdcBack, hOldFont);
        DeleteObject(hFont);
    }

    // 目标屏闪烁边框：选区阶段绕着屏幕 4 边画橙色色带
    if (g_state == ST_SELECT && g_borderFlashOn) {
        const int T = 8;
        HBRUSH hBrO = CreateSolidBrush(RGB(255, 160, 40));
        RECT rT = { 0, 0, g_scrW, T };
        RECT rB = { 0, g_scrH - T, g_scrW, g_scrH };
        RECT rL = { 0, 0, T, g_scrH };
        RECT rR = { g_scrW - T, 0, g_scrW, g_scrH };
        FillRect(hdcBack, &rT, hBrO);
        FillRect(hdcBack, &rB, hBrO);
        FillRect(hdcBack, &rL, hBrO);
        FillRect(hdcBack, &rR, hBrO);
        DeleteObject(hBrO);
    }

    BitBlt(hdc, 0, 0, g_scrW, g_scrH, hdcBack, 0, 0, SRCCOPY);
    SelectObject(hdcSrc, hbmOldSrc); DeleteDC(hdcSrc);
    SelectObject(hdcBack, hbmOldBack); DeleteObject(hbmBack); DeleteDC(hdcBack);
}

// 检测点击了哪个工具栏按钮，返回 id，-1 表示未命中
static int HitTestToolbar(POINT ptOverlay) {
    for (int i = 0; i < BTN_COUNT; i++)
        if (PtInRect(&g_btns[i].rc, ptOverlay)) return g_btns[i].id;
    return -1;
}

// --- 窗口过程 ---
static LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        PaintOverlay(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt;
        GetCursorPos(&pt);
        if (g_state == ST_SELECT) {
            if (g_dragging) g_ptEnd = pt;
            else g_rcHighlight = FindWindowRectAtPoint(pt);
        } else if (g_drawing) {
            g_drawP2 = pt;
            if (g_curTool == TL_BRUSH) {
                // 避免重复点积累
                if (g_curBrushPts.empty() ||
                    g_curBrushPts.back().x != pt.x || g_curBrushPts.back().y != pt.y) {
                    g_curBrushPts.push_back(pt);
                }
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt;
        GetCursorPos(&pt);
        if (g_state == ST_SELECT) {
            g_ptStart = g_ptEnd = pt;
            g_dragging = TRUE;
            SetCapture(hwnd);
        } else {
            // 编辑模式：先检查工具栏
            POINT ptOv = { pt.x - g_scrX, pt.y - g_scrY };
            int btnId = HitTestToolbar(ptOv);
            if (btnId == 0) { g_curTool = TL_ARROW; InvalidateRect(hwnd, NULL, FALSE); }
            else if (btnId == 1) { g_curTool = TL_RECT; InvalidateRect(hwnd, NULL, FALSE); }
            else if (btnId == 2) { g_curTool = TL_BRUSH; InvalidateRect(hwnd, NULL, FALSE); }
            else if (btnId == 3) { FinishCapture(); DestroyWindow(hwnd); }
            else if (btnId == 4) { DestroyWindow(hwnd); }
            else if (g_curTool != TL_NONE) {
                // 开始绘制标注
                g_drawing = true;
                g_drawP1 = g_drawP2 = pt;
                if (g_curTool == TL_BRUSH) {
                    g_curBrushPts.clear();
                    g_curBrushPts.push_back(pt);
                }
                SetCapture(hwnd);
            }
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        POINT pt;
        GetCursorPos(&pt);
        if (g_state == ST_SELECT) {
            ReleaseCapture();
            g_ptEnd = pt;
            int dx = abs(g_ptEnd.x - g_ptStart.x);
            int dy = abs(g_ptEnd.y - g_ptStart.y);
            if (dx < 5 && dy < 5) {
                g_selRect = FindWindowRectAtPoint(g_ptEnd);
                if (g_selRect.right <= g_selRect.left) { g_dragging = FALSE; return 0; }
            } else {
                g_selRect.left   = (std::min)(g_ptStart.x, g_ptEnd.x);
                g_selRect.top    = (std::min)(g_ptStart.y, g_ptEnd.y);
                g_selRect.right  = (std::max)(g_ptStart.x, g_ptEnd.x);
                g_selRect.bottom = (std::max)(g_ptStart.y, g_ptEnd.y);
            }
            g_dragging = FALSE;
            if (ReadAutoFinishOnSelectConfig()) {
                FinishCapture();
                DestroyWindow(hwnd);
                return 0;
            }
            g_state = ST_EDIT;
            g_curTool = TL_NONE;
            g_annos.clear();
            InitToolbar();
            InvalidateRect(hwnd, NULL, FALSE);
        } else if (g_drawing) {
            ReleaseCapture();
            g_drawP2 = pt;
            if (g_curTool == TL_BRUSH) {
                if (g_curBrushPts.empty() ||
                    g_curBrushPts.back().x != pt.x || g_curBrushPts.back().y != pt.y) {
                    g_curBrushPts.push_back(pt);
                }
                if (!g_curBrushPts.empty()) {
                    Anno a;
                    a.type = TL_BRUSH;
                    a.p1 = g_curBrushPts.front();
                    a.p2 = g_curBrushPts.back();
                    a.pts = g_curBrushPts;
                    g_annos.push_back(std::move(a));
                }
                g_curBrushPts.clear();
            } else {
                // 保存标注
                if (abs(g_drawP2.x - g_drawP1.x) > 3 || abs(g_drawP2.y - g_drawP1.y) > 3)
                    g_annos.push_back({ g_curTool, g_drawP1, g_drawP2, {} });
            }
            g_drawing = false;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_RBUTTONDOWN:
        // 吃掉 down，避免 DestroyWindow 后 up 落到桌面触发上下文菜单
        return 0;
    case WM_RBUTTONUP:
        if (g_state == ST_SELECT) {
            // 选区阶段右键 = 取消
            g_dragging = FALSE;
            DestroyWindow(hwnd);
        } else if (g_state == ST_EDIT) {
            POINT ptClick;
            GetCursorPos(&ptClick);
            if (PtInRect(&g_selRect, ptClick)) {
                if (g_annos.empty()) {
                    // 选区内无标注：右键 = 取消
                    DestroyWindow(hwnd);
                } else {
                    // 选区内有标注：撤销最后一个
                    g_annos.pop_back();
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            } else {
                // 选区外：完成截图
                FinishCapture();
                DestroyWindow(hwnd);
            }
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) {
            g_dragging = FALSE;
            g_drawing = false;
            g_curBrushPts.clear();
            DestroyWindow(hwnd);
        }
        // Ctrl+Z 撤销
        if (wParam == 'Z' && (GetAsyncKeyState(VK_CONTROL) & 0x8000)) {
            if (g_state == ST_EDIT && !g_annos.empty()) {
                g_annos.pop_back();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    case WM_TIMER:
        if (wParam == OVERLAY_FLASH_ID) {
            g_borderFlashOn = !g_borderFlashOn;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, OVERLAY_FLASH_ID);
        g_hOverlay = NULL;
        g_state = ST_SELECT;
        g_annos.clear();
        g_curBrushPts.clear();
        if (g_hScreenBmp) { DeleteObject(g_hScreenBmp); g_hScreenBmp = NULL; }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// --- 倒计时窗口 ---
static HWND g_hCountdown = NULL;
static int  g_countdownLeft = 0;
static HWND g_countdownParent = NULL;
#define COUNTDOWN_TICK_ID  1
#define COUNTDOWN_FIRE_ID  2
#define COUNTDOWN_MOVE_ID  3

static LRESULT CALLBACK CountdownProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        HBRUSH hBr = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rc, hBr); DeleteObject(hBr);
        HPEN hPen = CreatePen(PS_SOLID, 3, RGB(0, 174, 255));
        HPEN hOP = (HPEN)SelectObject(hdc, hPen);
        HBRUSH hOB = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, 0, 0, rc.right, rc.bottom);
        SelectObject(hdc, hOP); SelectObject(hdc, hOB); DeleteObject(hPen);
        HFONT hFont = CreateFontW(120, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));
        wchar_t buf[8]; swprintf_s(buf, L"%d", g_countdownLeft);
        DrawTextW(hdc, buf, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOldFont); DeleteObject(hFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
        if (wParam == COUNTDOWN_TICK_ID) {
            g_countdownLeft--;
            if (g_countdownLeft <= 0) {
                KillTimer(hwnd, COUNTDOWN_TICK_ID);
                KillTimer(hwnd, COUNTDOWN_MOVE_ID);
                ShowWindow(hwnd, SW_HIDE);
                // 让 DWM 合成后再抓屏，避免数字被拍进截图
                SetTimer(hwnd, COUNTDOWN_FIRE_ID, 80, NULL);
            } else {
                InvalidateRect(hwnd, NULL, TRUE);
            }
        } else if (wParam == COUNTDOWN_MOVE_ID) {
            // 跟随鼠标所在屏
            POINT pt; GetCursorPos(&pt);
            HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi = { sizeof(mi) };
            GetMonitorInfo(hMon, &mi);
            RECT rc; GetWindowRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int monW = mi.rcMonitor.right - mi.rcMonitor.left;
            int newX = mi.rcMonitor.left + (monW - w) / 2;
            int newY = mi.rcMonitor.top  + 60;
            if (rc.left != newX || rc.top != newY) {
                SetWindowPos(hwnd, HWND_TOPMOST, newX, newY, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
            }
        } else if (wParam == COUNTDOWN_FIRE_ID) {
            KillTimer(hwnd, COUNTDOWN_FIRE_ID);
            HWND parent = g_countdownParent;
            DestroyWindow(hwnd);
            StartCapture(parent, 0);
        }
        return 0;
    case WM_DESTROY:
        g_hCountdown = NULL;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void StartCountdown(HWND hParent, int seconds) {
    if (g_hCountdown) return;
    g_countdownParent = hParent;
    g_countdownLeft = seconds;

    POINT pt; GetCursorPos(&pt);
    HMONITOR hMon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = CountdownProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        wc.lpszClassName = L"AdvPasteCountdown";
        if (!RegisterClassExW(&wc)) {
            StartCapture(hParent, 0);
            return;
        }
        registered = true;
    }

    const int W = 180, H = 180;
    int monW = mi.rcMonitor.right - mi.rcMonitor.left;
    int x = mi.rcMonitor.left + (monW - W) / 2;
    int y = mi.rcMonitor.top  + 60;

    g_hCountdown = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        L"AdvPasteCountdown", NULL,
        WS_POPUP,
        x, y, W, H,
        NULL, NULL, GetModuleHandleW(NULL), NULL);

    if (!g_hCountdown) {
        StartCapture(hParent, 0);
        return;
    }

    ShowWindow(g_hCountdown, SW_SHOWNOACTIVATE);
    SetWindowPos(g_hCountdown, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    UpdateWindow(g_hCountdown);
    SetTimer(g_hCountdown, COUNTDOWN_TICK_ID, 1000, NULL);
    SetTimer(g_hCountdown, COUNTDOWN_MOVE_ID, 100, NULL);
}

void StartCapture(HWND hParent, int delaySec) {
    if (g_hOverlay || g_hCountdown) return;
    if (delaySec > 0) { StartCountdown(hParent, delaySec); return; }
    g_hParent = hParent;
    g_state = ST_SELECT;
    g_dragging = FALSE;
    g_drawing = false;
    g_curTool = TL_NONE;
    g_annos.clear();
    g_curBrushPts.clear();
    SetRectEmpty(&g_rcHighlight);
    SetRectEmpty(&g_selRect);

    // 只抓鼠标所在那块屏
    POINT cur; GetCursorPos(&cur);
    HMONITOR hMon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    GetMonitorInfo(hMon, &mi);
    g_scrX = mi.rcMonitor.left;
    g_scrY = mi.rcMonitor.top;
    g_scrW = mi.rcMonitor.right  - mi.rcMonitor.left;
    g_scrH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    g_hScreenBmp = CreateCompatibleBitmap(hdcScreen, g_scrW, g_scrH);
    SelectObject(hdcMem, g_hScreenBmp);
    BitBlt(hdcMem, 0, 0, g_scrW, g_scrH, hdcScreen, g_scrX, g_scrY, SRCCOPY);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    g_wnds.clear();
    EnumWindows(EnumWndProc, 0);

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = GetModuleHandleW(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_CROSS);
        wc.lpszClassName = L"AdvPasteOverlay";
        RegisterClassExW(&wc);
        registered = true;
    }

    g_hOverlay = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"AdvPasteOverlay", NULL,
        WS_POPUP | WS_VISIBLE,
        g_scrX, g_scrY, g_scrW, g_scrH,
        NULL, NULL, GetModuleHandleW(NULL), NULL);

    if (!g_hOverlay) return;

    SetForegroundWindow(g_hOverlay);
    SetFocus(g_hOverlay);

    g_borderFlashOn = true;
    SetTimer(g_hOverlay, OVERLAY_FLASH_ID, 450, NULL);
}
