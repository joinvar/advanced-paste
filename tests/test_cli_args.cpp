#include <gtest/gtest.h>
#include "cli_args.h"

// ── JsonEscape ──────────────────────────────────────────────

TEST(JsonEscape, PlainAsciiPassThrough) {
    EXPECT_EQ(JsonEscape(L"hello world"), L"hello world");
}
TEST(JsonEscape, EscapesQuoteAndBackslash) {
    EXPECT_EQ(JsonEscape(L"a\"b\\c"), L"a\\\"b\\\\c");
}
TEST(JsonEscape, EscapesNewlineTabCR) {
    EXPECT_EQ(JsonEscape(L"a\nb\tc\rd"), L"a\\nb\\tc\\rd");
}
TEST(JsonEscape, EscapesControlCharsAsUnicode) {
    std::wstring in;
    in += (wchar_t)0x01;
    in += (wchar_t)0x1F;
    EXPECT_EQ(JsonEscape(in), L"\\u0001\\u001f");
}
TEST(JsonEscape, PreservesUnicodeAbove0x20) {
    std::wstring in = L"中文 path";
    EXPECT_EQ(JsonEscape(in), in);
}
TEST(JsonEscape, EmptyString) {
    EXPECT_EQ(JsonEscape(L""), L"");
}

// ── ParseRect ───────────────────────────────────────────────

TEST(ParseRect, Valid) {
    int x, y, w, h;
    EXPECT_TRUE(ParseRect(L"10,20,300,400", &x, &y, &w, &h));
    EXPECT_EQ(x, 10); EXPECT_EQ(y, 20); EXPECT_EQ(w, 300); EXPECT_EQ(h, 400);
}
TEST(ParseRect, AllowsNegativeOrigin) {
    int x, y, w, h;
    EXPECT_TRUE(ParseRect(L"-1920,0,1920,1080", &x, &y, &w, &h));
    EXPECT_EQ(x, -1920);
}
TEST(ParseRect, RejectsTooFewFields) {
    int x, y, w, h;
    EXPECT_FALSE(ParseRect(L"10,20,30", &x, &y, &w, &h));
}
TEST(ParseRect, RejectsNonNumeric) {
    int x, y, w, h;
    EXPECT_FALSE(ParseRect(L"10,20,abc,40", &x, &y, &w, &h));
}
TEST(ParseRect, RejectsNull) {
    int x, y, w, h;
    EXPECT_FALSE(ParseRect(nullptr, &x, &y, &w, &h));
}

// ── ParseXY ─────────────────────────────────────────────────

TEST(ParseXY, Valid) {
    int x, y;
    EXPECT_TRUE(ParseXY(L"100,200", &x, &y));
    EXPECT_EQ(x, 100); EXPECT_EQ(y, 200);
}
TEST(ParseXY, RejectsExtraFields) {
    // 多余字段不报错（swscanf_s 读够两个就停），但前两个必须正确
    int x, y;
    EXPECT_TRUE(ParseXY(L"5,10,99", &x, &y));
    EXPECT_EQ(x, 5); EXPECT_EQ(y, 10);
}
TEST(ParseXY, RejectsSingleField) {
    int x, y;
    EXPECT_FALSE(ParseXY(L"42", &x, &y));
}
TEST(ParseXY, RejectsNull) {
    int x, y;
    EXPECT_FALSE(ParseXY(nullptr, &x, &y));
}

// ── ParseKeyName ────────────────────────────────────────────

TEST(ParseKeyName, Modifiers) {
    WORD vk = 0;
    EXPECT_TRUE(ParseKeyName(L"ctrl", &vk));     EXPECT_EQ(vk, (WORD)VK_CONTROL);
    EXPECT_TRUE(ParseKeyName(L"Control", &vk));  EXPECT_EQ(vk, (WORD)VK_CONTROL);
    EXPECT_TRUE(ParseKeyName(L"alt", &vk));      EXPECT_EQ(vk, (WORD)VK_MENU);
    EXPECT_TRUE(ParseKeyName(L"SHIFT", &vk));    EXPECT_EQ(vk, (WORD)VK_SHIFT);
    EXPECT_TRUE(ParseKeyName(L"win", &vk));      EXPECT_EQ(vk, (WORD)VK_LWIN);
}
TEST(ParseKeyName, CaseInsensitive) {
    WORD vk = 0;
    EXPECT_TRUE(ParseKeyName(L"EsC", &vk));      EXPECT_EQ(vk, (WORD)VK_ESCAPE);
    EXPECT_TRUE(ParseKeyName(L"Escape", &vk));   EXPECT_EQ(vk, (WORD)VK_ESCAPE);
}
TEST(ParseKeyName, SpecialKeys) {
    WORD vk = 0;
    EXPECT_TRUE(ParseKeyName(L"enter", &vk));    EXPECT_EQ(vk, (WORD)VK_RETURN);
    EXPECT_TRUE(ParseKeyName(L"return", &vk));   EXPECT_EQ(vk, (WORD)VK_RETURN);
    EXPECT_TRUE(ParseKeyName(L"tab", &vk));      EXPECT_EQ(vk, (WORD)VK_TAB);
    EXPECT_TRUE(ParseKeyName(L"space", &vk));    EXPECT_EQ(vk, (WORD)VK_SPACE);
    EXPECT_TRUE(ParseKeyName(L"backspace", &vk));EXPECT_EQ(vk, (WORD)VK_BACK);
    EXPECT_TRUE(ParseKeyName(L"delete", &vk));   EXPECT_EQ(vk, (WORD)VK_DELETE);
    EXPECT_TRUE(ParseKeyName(L"home", &vk));     EXPECT_EQ(vk, (WORD)VK_HOME);
    EXPECT_TRUE(ParseKeyName(L"pageup", &vk));   EXPECT_EQ(vk, (WORD)VK_PRIOR);
    EXPECT_TRUE(ParseKeyName(L"pagedown", &vk)); EXPECT_EQ(vk, (WORD)VK_NEXT);
}
TEST(ParseKeyName, Arrows) {
    WORD vk = 0;
    EXPECT_TRUE(ParseKeyName(L"up",    &vk)); EXPECT_EQ(vk, (WORD)VK_UP);
    EXPECT_TRUE(ParseKeyName(L"down",  &vk)); EXPECT_EQ(vk, (WORD)VK_DOWN);
    EXPECT_TRUE(ParseKeyName(L"left",  &vk)); EXPECT_EQ(vk, (WORD)VK_LEFT);
    EXPECT_TRUE(ParseKeyName(L"right", &vk)); EXPECT_EQ(vk, (WORD)VK_RIGHT);
}
TEST(ParseKeyName, FunctionKeys) {
    WORD vk = 0;
    EXPECT_TRUE(ParseKeyName(L"F1",  &vk)); EXPECT_EQ(vk, (WORD)VK_F1);
    EXPECT_TRUE(ParseKeyName(L"f12", &vk)); EXPECT_EQ(vk, (WORD)VK_F12);
    EXPECT_TRUE(ParseKeyName(L"f24", &vk)); EXPECT_EQ(vk, (WORD)(VK_F1 + 23));
}
TEST(ParseKeyName, LettersAndDigits) {
    WORD vk = 0;
    EXPECT_TRUE(ParseKeyName(L"a", &vk)); EXPECT_EQ(vk, (WORD)'A');
    EXPECT_TRUE(ParseKeyName(L"Z", &vk)); EXPECT_EQ(vk, (WORD)'Z');
    EXPECT_TRUE(ParseKeyName(L"0", &vk)); EXPECT_EQ(vk, (WORD)'0');
    EXPECT_TRUE(ParseKeyName(L"9", &vk)); EXPECT_EQ(vk, (WORD)'9');
}
TEST(ParseKeyName, RejectsUnknown) {
    WORD vk = 0;
    EXPECT_FALSE(ParseKeyName(L"notakey", &vk));
    EXPECT_FALSE(ParseKeyName(L"", &vk));
    EXPECT_FALSE(ParseKeyName(L"f0", &vk));
    EXPECT_FALSE(ParseKeyName(L"f25", &vk));
    EXPECT_FALSE(ParseKeyName(L"ab", &vk)); // 两个字母不是有效主键
}

// ── ParseButton ─────────────────────────────────────────────

TEST(ParseButton, Left) {
    DWORD d = 0, u = 0;
    EXPECT_TRUE(ParseButton(L"left", &d, &u));
    EXPECT_EQ(d, (DWORD)MOUSEEVENTF_LEFTDOWN);
    EXPECT_EQ(u, (DWORD)MOUSEEVENTF_LEFTUP);
}
TEST(ParseButton, Right) {
    DWORD d = 0, u = 0;
    EXPECT_TRUE(ParseButton(L"right", &d, &u));
    EXPECT_EQ(d, (DWORD)MOUSEEVENTF_RIGHTDOWN);
}
TEST(ParseButton, Middle) {
    DWORD d = 0, u = 0;
    EXPECT_TRUE(ParseButton(L"middle", &d, &u));
    EXPECT_EQ(d, (DWORD)MOUSEEVENTF_MIDDLEDOWN);
}
TEST(ParseButton, NullDefaultsToLeft) {
    DWORD d = 0, u = 0;
    EXPECT_TRUE(ParseButton(nullptr, &d, &u));
    EXPECT_EQ(d, (DWORD)MOUSEEVENTF_LEFTDOWN);
}
TEST(ParseButton, RejectsUnknown) {
    DWORD d = 0, u = 0;
    EXPECT_FALSE(ParseButton(L"x1", &d, &u));
}

// ── FindOpt / HasFlag ───────────────────────────────────────

TEST(FindOpt, FindsValueAfterFlag) {
    wchar_t* argv[] = { (wchar_t*)L"app", (wchar_t*)L"--out", (wchar_t*)L"file.png" };
    EXPECT_STREQ(FindOpt(3, argv, L"--out"), L"file.png");
}
TEST(FindOpt, ReturnsNullWhenFlagMissing) {
    wchar_t* argv[] = { (wchar_t*)L"app", (wchar_t*)L"--other" };
    EXPECT_EQ(FindOpt(2, argv, L"--out"), nullptr);
}
TEST(FindOpt, ReturnsNullWhenNoTrailingValue) {
    wchar_t* argv[] = { (wchar_t*)L"app", (wchar_t*)L"--out" };
    EXPECT_EQ(FindOpt(2, argv, L"--out"), nullptr);
}
TEST(FindOpt, FirstMatchWins) {
    wchar_t* argv[] = { (wchar_t*)L"app", (wchar_t*)L"--v", (wchar_t*)L"a",
                        (wchar_t*)L"--v", (wchar_t*)L"b" };
    EXPECT_STREQ(FindOpt(5, argv, L"--v"), L"a");
}

TEST(HasFlag, DetectsPresence) {
    wchar_t* argv[] = { (wchar_t*)L"app", (wchar_t*)L"--fullscreen" };
    EXPECT_TRUE(HasFlag(2, argv, L"--fullscreen"));
    EXPECT_FALSE(HasFlag(2, argv, L"--other"));
}
TEST(HasFlag, EmptyArgs) {
    EXPECT_FALSE(HasFlag(0, nullptr, L"--x"));
}
