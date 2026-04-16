#include <gtest/gtest.h>
#include "utils.h"
#include <filesystem>

namespace fs = std::filesystem;

// ── GDI+ 全局初始化 ─────────────────────────────────────────
class GdiplusEnv : public ::testing::Environment {
    ULONG_PTR token_ = 0;

public:
    void SetUp() override {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    }
    void TearDown() override {
        Gdiplus::GdiplusShutdown(token_);
        CoUninitialize();
    }
};
auto* const g_gdipEnv = ::testing::AddGlobalTestEnvironment(new GdiplusEnv);

// ── ParseHotkey ─────────────────────────────────────────────

TEST(ParseHotkey, CtrlAltX) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Ctrl+Alt+X", &mod, &vk));
    EXPECT_EQ(mod, (UINT)(MOD_CONTROL | MOD_ALT));
    EXPECT_EQ(vk, (UINT)'X');
}

TEST(ParseHotkey, ShiftA) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Shift+A", &mod, &vk));
    EXPECT_EQ(mod, (UINT)MOD_SHIFT);
    EXPECT_EQ(vk, (UINT)'A');
}

TEST(ParseHotkey, SingleLetterNoModifier) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"A", &mod, &vk));
    EXPECT_EQ(mod, 0u);
    EXPECT_EQ(vk, (UINT)'A');
}

TEST(ParseHotkey, SingleDigit) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"5", &mod, &vk));
    EXPECT_EQ(mod, 0u);
    EXPECT_EQ(vk, (UINT)'5');
}

TEST(ParseHotkey, FunctionKeyF1) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"F1", &mod, &vk));
    EXPECT_EQ(mod, 0u);
    EXPECT_EQ(vk, (UINT)VK_F1);
}

TEST(ParseHotkey, CtrlF12) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Ctrl+F12", &mod, &vk));
    EXPECT_EQ(mod, (UINT)MOD_CONTROL);
    EXPECT_EQ(vk, (UINT)VK_F12);
}

TEST(ParseHotkey, F24) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"F24", &mod, &vk));
    EXPECT_EQ(vk, (UINT)(VK_F1 + 23));
}

TEST(ParseHotkey, AllModifiers) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Ctrl+Alt+Shift+Win+Z", &mod, &vk));
    EXPECT_EQ(mod, (UINT)(MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN));
    EXPECT_EQ(vk, (UINT)'Z');
}

TEST(ParseHotkey, ControlAlias) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Control+X", &mod, &vk));
    EXPECT_EQ(mod, (UINT)MOD_CONTROL);
    EXPECT_EQ(vk, (UINT)'X');
}

TEST(ParseHotkey, WinModifier) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Win+D", &mod, &vk));
    EXPECT_EQ(mod, (UINT)MOD_WIN);
    EXPECT_EQ(vk, (UINT)'D');
}

TEST(ParseHotkey, CaseInsensitive) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"ctrl+alt+x", &mod, &vk));
    EXPECT_EQ(mod, (UINT)(MOD_CONTROL | MOD_ALT));
    EXPECT_EQ(vk, (UINT)'X');
}

TEST(ParseHotkey, SpacesTrimmed) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Ctrl + Alt + X", &mod, &vk));
    EXPECT_EQ(mod, (UINT)(MOD_CONTROL | MOD_ALT));
    EXPECT_EQ(vk, (UINT)'X');
}

TEST(ParseHotkey, DigitWithModifier) {
    UINT mod = 0, vk = 0;
    EXPECT_TRUE(ParseHotkey(L"Ctrl+0", &mod, &vk));
    EXPECT_EQ(mod, (UINT)MOD_CONTROL);
    EXPECT_EQ(vk, (UINT)'0');
}

TEST(ParseHotkey, EmptyStringFails) {
    UINT mod = 0, vk = 0;
    EXPECT_FALSE(ParseHotkey(L"", &mod, &vk));
}

TEST(ParseHotkey, OnlyModifierFails) {
    UINT mod = 0, vk = 0;
    EXPECT_FALSE(ParseHotkey(L"Ctrl+Alt", &mod, &vk));
}

TEST(ParseHotkey, TrailingPlusFails) {
    UINT mod = 0, vk = 0;
    EXPECT_FALSE(ParseHotkey(L"Ctrl+", &mod, &vk));
}

TEST(ParseHotkey, UnknownModifierFails) {
    UINT mod = 0, vk = 0;
    EXPECT_FALSE(ParseHotkey(L"Super+X", &mod, &vk));
}

TEST(ParseHotkey, InvalidKeyFails) {
    UINT mod = 0, vk = 0;
    EXPECT_FALSE(ParseHotkey(L"Ctrl+INVALID", &mod, &vk));
}

TEST(ParseHotkey, F0OutOfRange) {
    UINT mod = 0, vk = 0;
    EXPECT_FALSE(ParseHotkey(L"F0", &mod, &vk));
}

TEST(ParseHotkey, F25OutOfRange) {
    UINT mod = 0, vk = 0;
    EXPECT_FALSE(ParseHotkey(L"F25", &mod, &vk));
}

// ── GetEncoderClsid ─────────────────────────────────────────

TEST(GetEncoderClsid, PngExists) {
    CLSID clsid;
    EXPECT_GE(GetEncoderClsid(L"image/png", &clsid), 0);
}

TEST(GetEncoderClsid, JpegExists) {
    CLSID clsid;
    EXPECT_GE(GetEncoderClsid(L"image/jpeg", &clsid), 0);
}

TEST(GetEncoderClsid, NonexistentReturnsNeg) {
    CLSID clsid;
    EXPECT_EQ(GetEncoderClsid(L"image/nonexistent", &clsid), -1);
}

// ── SaveBitmapAsPng ─────────────────────────────────────────

class SavePngTest : public ::testing::Test {
protected:
    fs::path tempDir;

    void SetUp() override {
        tempDir = fs::temp_directory_path() / "ap_test_png";
        fs::create_directories(tempDir);
    }
    void TearDown() override {
        fs::remove_all(tempDir);
    }

    HBITMAP CreateSmallBitmap(int w, int h) {
        HDC hdc = GetDC(NULL);
        HBITMAP hBmp = CreateCompatibleBitmap(hdc, w, h);
        ReleaseDC(NULL, hdc);
        return hBmp;
    }
};

TEST_F(SavePngTest, SavesValidPng) {
    HBITMAP hBmp = CreateSmallBitmap(4, 4);
    ASSERT_NE(hBmp, (HBITMAP)NULL);

    std::wstring result = SaveBitmapAsPng(hBmp, 4, 4, tempDir.wstring());
    DeleteObject(hBmp);

    ASSERT_FALSE(result.empty());
    EXPECT_TRUE(fs::exists(result));
    EXPECT_GT(fs::file_size(result), 0u);

    FILE* f = _wfopen(result.c_str(), L"rb");
    ASSERT_NE(f, nullptr);
    unsigned char hdr[4] = {};
    fread(hdr, 1, 4, f);
    fclose(f);
    EXPECT_EQ(hdr[0], 0x89);
    EXPECT_EQ(hdr[1], 0x50);
    EXPECT_EQ(hdr[2], 0x4E);
    EXPECT_EQ(hdr[3], 0x47);
}

TEST_F(SavePngTest, NullBitmapReturnsEmpty) {
    EXPECT_TRUE(SaveBitmapAsPng(NULL, 4, 4, tempDir.wstring()).empty());
}

TEST_F(SavePngTest, ZeroWidthReturnsEmpty) {
    HBITMAP hBmp = CreateSmallBitmap(4, 4);
    EXPECT_TRUE(SaveBitmapAsPng(hBmp, 0, 4, tempDir.wstring()).empty());
    DeleteObject(hBmp);
}

TEST_F(SavePngTest, ZeroHeightReturnsEmpty) {
    HBITMAP hBmp = CreateSmallBitmap(4, 4);
    EXPECT_TRUE(SaveBitmapAsPng(hBmp, 4, 0, tempDir.wstring()).empty());
    DeleteObject(hBmp);
}

TEST_F(SavePngTest, EmptyDirReturnsEmpty) {
    HBITMAP hBmp = CreateSmallBitmap(4, 4);
    EXPECT_TRUE(SaveBitmapAsPng(hBmp, 4, 4, L"").empty());
    DeleteObject(hBmp);
}

TEST_F(SavePngTest, AutoIncrementsFilename) {
    HBITMAP h1 = CreateSmallBitmap(2, 2);
    HBITMAP h2 = CreateSmallBitmap(2, 2);

    std::wstring p1 = SaveBitmapAsPng(h1, 2, 2, tempDir.wstring());
    std::wstring p2 = SaveBitmapAsPng(h2, 2, 2, tempDir.wstring());

    DeleteObject(h1);
    DeleteObject(h2);

    ASSERT_FALSE(p1.empty());
    ASSERT_FALSE(p2.empty());
    EXPECT_NE(p1, p2);
    EXPECT_TRUE(fs::exists(p1));
    EXPECT_TRUE(fs::exists(p2));
}

// ── Config 读写 ─────────────────────────────────────────────

class ConfigTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto p = fs::path(GetConfigPath());
        backup_ = p.parent_path() / "config.ini.test_bak";
        if (fs::exists(p))
            fs::copy_file(p, backup_, fs::copy_options::overwrite_existing);
        fs::remove(p);
    }
    void TearDown() override {
        auto p = fs::path(GetConfigPath());
        fs::remove(p);
        if (fs::exists(backup_))
            fs::rename(backup_, p);
    }

private:
    fs::path backup_;
};

TEST_F(ConfigTest, SaveDirRoundTrip) {
    WriteSaveDirConfig(L"C:\\TestScreenshots");
    EXPECT_EQ(ReadSaveDirConfig(), L"C:\\TestScreenshots");
}

TEST_F(ConfigTest, SaveDirEmpty) {
    WriteSaveDirConfig(L"");
    EXPECT_TRUE(ReadSaveDirConfig().empty());
}

TEST_F(ConfigTest, SaveDirTrimsTrailingSlash) {
    WriteSaveDirConfig(L"C:\\TestDir\\");
    EXPECT_EQ(ReadSaveDirConfig(), L"C:\\TestDir");
}

TEST_F(ConfigTest, AutoFinishRoundTrip) {
    WriteAutoFinishOnSelectConfig(true);
    EXPECT_TRUE(ReadAutoFinishOnSelectConfig());
    WriteAutoFinishOnSelectConfig(false);
    EXPECT_FALSE(ReadAutoFinishOnSelectConfig());
}

TEST_F(ConfigTest, DefaultHotkey) {
    EXPECT_EQ(ReadHotkeyConfig(), L"Ctrl+Alt+X");
    EXPECT_TRUE(fs::exists(GetConfigPath()));
}

TEST_F(ConfigTest, DelaySecondsDefault) {
    EnsureConfigDefaults();
    EXPECT_EQ(ReadDelaySecondsConfig(), 3);
}
