// ============================================================================
//  小转盘 (Lucky Wheel)  —  原生 C++ / Win32 + GDI+  单文件实现
//  特性：抗锯齿矢量转盘、渐变金属外圈、灯泡、玻璃高光、指针摆动、彩带粒子、
//        右侧可编辑列表（名称 / 权重 / 实时概率）、自动保存、音效
//  编译：cl /nologo /std:c++17 /O2 /MT /EHsc /DUNICODE /D_UNICODE wheel.cpp
//               /link /SUBSYSTEM:WINDOWS /OUT:小转盘.exe
// ============================================================================
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <windowsx.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <random>
#include <string>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace Gdiplus;

// ============================ 基础工具 ====================================

static const wchar_t* kTitle = L"小转盘";

static inline int Clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static inline float Clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline double Clampd(double v, double a, double b) { return v < a ? a : (v > b ? b : v); }

static inline Color RGBA(int r, int g, int b, int a = 255) {
    return Color((BYTE)Clampi(a, 0, 255), (BYTE)Clampi(r, 0, 255),
                 (BYTE)Clampi(g, 0, 255), (BYTE)Clampi(b, 0, 255));
}
static inline Color Alpha(Color c, int a) {
    return Color((BYTE)Clampi(a, 0, 255), c.GetR(), c.GetG(), c.GetB());
}
static inline Color ScaleA(Color c, float f) { return Alpha(c, (int)(c.GetA() * f)); }
static inline Color MixColor(Color a, Color b, float t) {
    t = Clampf(t, 0.f, 1.f);
    return Color((BYTE)(a.GetA() + (b.GetA() - a.GetA()) * t),
                 (BYTE)(a.GetR() + (b.GetR() - a.GetR()) * t),
                 (BYTE)(a.GetG() + (b.GetG() - a.GetG()) * t),
                 (BYTE)(a.GetB() + (b.GetB() - a.GetB()) * t));
}

static double NowSec() {
    static LARGE_INTEGER freq = {};
    static bool inited = false;
    if (!inited) { QueryPerformanceFrequency(&freq); inited = true; }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)freq.QuadPart;
}

static std::wstring TrimW(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == L' ' || s[a] == L'\t' || s[a] == L'\r' || s[a] == L'\n')) a++;
    while (b > a && (s[b - 1] == L' ' || s[b - 1] == L'\t' || s[b - 1] == L'\r' || s[b - 1] == L'\n')) b--;
    return s.substr(a, b - a);
}

static std::string WToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring Utf8ToW(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

static void RoundRectPath(GraphicsPath& p, RectF r, float rad) {
    float maxr = (r.Width < r.Height ? r.Width : r.Height) * 0.5f;
    if (rad > maxr) rad = maxr;
    if (rad < 0.5f) { p.AddRectangle(r); return; }
    float d = rad * 2.f;
    p.AddArc(r.X, r.Y, d, d, 180.f, 90.f);
    p.AddArc(r.GetRight() - d, r.Y, d, d, 270.f, 90.f);
    p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.f, 90.f);
    p.AddArc(r.X, r.GetBottom() - d, d, d, 90.f, 90.f);
    p.CloseFigure();
}

static void FillRound(Graphics& g, RectF r, float rad, Brush* b) {
    GraphicsPath p;
    RoundRectPath(p, r, rad);
    g.FillPath(b, &p);
}
static void StrokeRound(Graphics& g, RectF r, float rad, Pen* pen) {
    GraphicsPath p;
    RoundRectPath(p, r, rad);
    g.DrawPath(pen, &p);
}

static void GlowEllipse(Graphics& g, PointF c, float rx, float ry, Color inner, Color outer) {
    GraphicsPath p;
    p.AddEllipse(c.X - rx, c.Y - ry, rx * 2.f, ry * 2.f);
    PathGradientBrush b(&p);
    b.SetCenterPoint(c);
    b.SetCenterColor(inner);
    Color sc[1] = { outer };
    int n = 1;
    b.SetSurroundColors(sc, &n);
    b.SetFocusScales(0.f, 0.f);
    g.FillPath(&b, &p);
}

static void MeasureS(Graphics& g, const std::wstring& s, Font* f, float* w, float* h) {
    if (s.empty()) { if (w) *w = 0; if (h) *h = 0; return; }
    StringFormat fmt;
    fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    RectF box(0, 0, 4000, 4000);
    RectF out;
    g.MeasureString(s.c_str(), (INT)s.size(), f, box, &fmt, &out);
    if (w) *w = out.Width;
    if (h) *h = out.Height;
}

static std::wstring FitText(Graphics& g, const std::wstring& s, Font* f, float maxW) {
    float w = 0, h = 0;
    MeasureS(g, s, f, &w, &h);
    if (w <= maxW || s.empty()) return s;
    std::wstring out = s;
    while (out.size() > 1) {
        out.pop_back();
        std::wstring t = out + L"…";
        MeasureS(g, t, f, &w, &h);
        if (w <= maxW) return t;
    }
    return L"…";
}

// 以像素为单位绘制一行文字，支持阴影
static void DrawTextS(Graphics& g, const std::wstring& s, Font* f, RectF r, Color col,
                      StringAlignment ha = StringAlignmentNear,
                      StringAlignment va = StringAlignmentCenter, bool wrap = false,
                      REAL sdx = 0, REAL sdy = 0, Color shadow = Color(0, 0, 0, 0)) {
    if (s.empty()) return;
    StringFormat fmt;
    fmt.SetAlignment(ha);
    fmt.SetLineAlignment(va);
    fmt.SetTrimming(StringTrimmingEllipsisCharacter);
    if (!wrap) fmt.SetFormatFlags(StringFormatFlagsNoWrap);
    if (shadow.GetA() > 0) {
        SolidBrush sb(shadow);
        RectF rs(r.X + sdx, r.Y + sdy, r.Width, r.Height);
        g.DrawString(s.c_str(), (INT)s.size(), f, rs, &fmt, &sb);
    }
    SolidBrush b(col);
    g.DrawString(s.c_str(), (INT)s.size(), f, r, &fmt, &b);
}

static std::wstring FmtPct(double p) {
    wchar_t buf[32];
    if (p >= 10.0 - 1e-9) swprintf(buf, 32, L"%.1f%%", p);
    else if (p >= 1.0) swprintf(buf, 32, L"%.2f%%", p);
    else swprintf(buf, 32, L"%.3f%%", p);
    return buf;
}
static std::wstring FmtWeight(double w) {
    wchar_t buf[48];
    if (fabs(w - floor(w + 0.5)) < 1e-6) swprintf(buf, 48, L"%.0f", w);
    else swprintf(buf, 48, L"%.2f", w);
    return buf;
}

// ============================ 数据模型 ====================================

struct Item {
    std::wstring name;
    double w = 1.0;
};

struct Particle {
    float x, y, vx, vy;
    float rot, vrot;
    float size, life, maxLife;
    Color col;
};

static const int kPaletteN = 12;
static Color PaletteColor(int i) {
    static const int P[kPaletteN][3] = {
        {247, 107, 107},  // 珊瑚红
        { 77, 171, 247},  // 天蓝
        {255, 212,  59},  // 明黄
        { 81, 207, 102},  // 草绿
        {177, 151, 252},  // 紫罗兰
        {255, 169,  77},  // 橙
        { 34, 184, 207},  // 青
        {247, 131, 172},  // 粉
        {148, 216,  45},  // 青柠
        {116, 143, 252},  // 靛蓝
        { 56, 217, 169},  // 蓝绿
        {229, 153, 247},  // 品红
    };
    int k = ((i % kPaletteN) + kPaletteN) % kPaletteN;
    Color c = RGBA(P[k][0], P[k][1], P[k][2]);
    if ((i / kPaletteN) % 2 == 1) c = MixColor(c, RGBA(20, 22, 38), 0.22f);
    return c;
}

struct Layout {
    float S = 1.f;
    float W = 0, H = 0;
    RectF panel;
    RectF wheelArea;
    PointF wc;             // 转盘圆心
    float R = 200.f;       // 彩色盘面半径
    float rowH = 44.f, rowGap = 6.f;
    float listTop = 0, listBottom = 0, listX = 0, listW = 0;
    RectF btnAdd, btnPreset, btnReset, btnMute;
    float hubR = 40.f;
};

struct ButtonHit { enum { NONE = -1, ADD = 0, PRESET, RESET, MUTE, DELETE_BASE = 100 }; };

struct App {
    HWND hwnd = nullptr;
    std::vector<Item> items;
    int sel = -1;
    std::wstring lastResult;

    // 动画状态
    double rot = 0;            // 角度（度，顺时针）
    bool spinning = false;
    double rot0 = 0, rotDelta = 0, spinT0 = 0, spinDur = 4.4;
    double angSpeed = 0;       // 度/秒
    int pendingWinner = -1;
    int winner = -1;
    double resultT = 0;        // 结果卡片存活时间
    std::vector<Particle> parts;
    float wob = 0;
    float wobV = 0;
    int lastTickIdx = -1;
    double lastTickT = 0;

    // 交互
    int hoverRow = -1;
    int hoverBtn = ButtonHit::NONE;
    bool hoverWheel = false;
    float scroll = 0;
    bool soundOn = true;
    double lastFrameT = 0;

    // 编辑框
    HWND hEdit = nullptr;
    int editRow = -1, editField = -1;   // field: 0=名称 1=权重
    HFONT hFontUI = nullptr;
    HBRUSH editBrush = nullptr;
    float editFontPx = 0;

    // 缓存
    Bitmap* layer = nullptr;
    bool layerDirty = true;
    Bitmap* wheel = nullptr;
    float wheelBuiltR = 0;
    bool wheelDirty = true;
    double lastResizeT = 0;
    Layout L;

    // 字体
    Font *fTitle = nullptr, *fSub = nullptr, *fRow = nullptr, *fSmall = nullptr;
    Font *fCol = nullptr, *fBtn = nullptr, *fHub = nullptr, *fToastName = nullptr;
    Font *fToastSmall = nullptr, *fBig = nullptr;
    float fontScale = -1.f;

    // 音效
    std::vector<BYTE> sndTick, sndWin;
    float speedForBlur = 0.f;

    std::mt19937_64 rng;
    bool shotMode = false;
};

static App g;

// ============================ 配置读写 ====================================

static std::wstring ExeDir() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH * 2);
    std::wstring s(buf, n);
    size_t p = s.find_last_of(L"\\/");
    if (p != std::wstring::npos) s = s.substr(0, p);
    return s;
}

static std::wstring ConfigPath() {
    return ExeDir() + L"\\小转盘配置.txt";
}

struct Preset { const wchar_t* name; const wchar_t* items[10]; double w[10]; int n; };

static Preset kPresets[] = {
    { L"今天吃什么", { L"火锅", L"烧烤", L"日料", L"麻辣烫", L"汉堡", L"饺子", L"米线", L"沙拉" },
      { 3, 2, 2, 2, 1, 1, 1, 1 }, 8 },
    { L"周末去哪儿", { L"看电影", L"爬山", L"逛商场", L"图书馆", L"露营", L"睡一整天", L"咖啡探店", L"骑行" },
      { 2, 1, 2, 1, 2, 3, 1, 1 }, 8 },
    { L"今天学什么", { L"高数", L"英语", L"专业课", L"刷题", L"复习错题", L"休息一下" },
      { 3, 2, 3, 2, 2, 1 }, 6 },
    { L"谁来买单", { L"我买单", L"你买单", L"他买单", L"AA 制", L"掷骰子", L"下次再说" },
      { 1, 1, 1, 1, 1, 1 }, 6 },
    { L"真心话大冒险", { L"真心话", L"大冒险", L"唱首歌", L"表演才艺", L"夸夸左边的人", L"免罚一次" },
      { 1, 1, 1, 1, 1, 1 }, 6 },
};
static const int kPresetCount = 5;

static void LoadPreset(int idx) {
    if (idx < 0 || idx >= kPresetCount) idx = 0;
    g.items.clear();
    for (int i = 0; i < kPresets[idx].n; i++) {
        Item it;
        it.name = kPresets[idx].items[i];
        it.w = kPresets[idx].w[i];
        g.items.push_back(it);
    }
    g.sel = g.items.empty() ? -1 : 0;
    g.scroll = 0;
    g.wheelDirty = true;
    g.layerDirty = true;
}

static void SaveConfig() {
    std::wstring p = ConfigPath();
    FILE* f = _wfopen(p.c_str(), L"wb");
    if (!f) return;
    const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
    fwrite(bom, 1, 3, f);
    std::string head = "# 小转盘配置：每行一条，格式为  名称=权重\n"
                       "# 权重越大，命中概率越高；可直接用记事本修改\n";
    fwrite(head.data(), 1, head.size(), f);
    for (size_t i = 0; i < g.items.size(); i++) {
        std::string line = WToUtf8(g.items[i].name) + "=" + WToUtf8(FmtWeight(g.items[i].w)) + "\n";
        fwrite(line.data(), 1, line.size(), f);
    }
    fclose(f);
}

static bool LoadConfig() {
    std::wstring p = ConfigPath();
    FILE* f = _wfopen(p.c_str(), L"rb");
    if (!f) return false;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF) data = data.substr(3);

    std::vector<Item> out;
    size_t pos = 0;
    while (pos <= data.size()) {
        size_t e = data.find('\n', pos);
        if (e == std::string::npos) e = data.size();
        std::string line = data.substr(pos, e - pos);
        pos = e + 1;
        std::wstring w = TrimW(Utf8ToW(line));
        if (w.empty() || w[0] == L'#') { if (e >= data.size()) break; continue; }
        size_t eq = w.find_last_of(L'=');
        std::wstring name; double wt = 1.0;
        if (eq == std::wstring::npos) {
            name = w;
        } else {
            name = TrimW(w.substr(0, eq));
            std::wstring ws = TrimW(w.substr(eq + 1));
            if (!ws.empty() && ws[ws.size() - 1] == L'%') ws = TrimW(ws.substr(0, ws.size() - 1));
            wchar_t* endp = nullptr;
            double v = wcstod(ws.c_str(), &endp);
            if (endp && endp != ws.c_str() && v > 0) wt = v;
        }
        if (name.empty()) { if (e >= data.size()) break; continue; }
        if (name.size() > 24) name = name.substr(0, 24);
        Item it; it.name = name; it.w = Clampd(wt, 0.0001, 1e7);
        out.push_back(it);
        if (e >= data.size()) break;
        if (out.size() >= 60) break;
    }
    if (out.empty()) return false;
    g.items = out;
    g.sel = 0;
    return true;
}

// ============================ 音效 ========================================

static std::vector<BYTE> MakeWav(const std::vector<float>& smp, int rate) {
    std::vector<BYTE> w;
    int n = (int)smp.size();
    DWORD dataSize = (DWORD)(n * 2);
    auto put16 = [&](int v) { w.push_back((BYTE)(v & 0xFF)); w.push_back((BYTE)((v >> 8) & 0xFF)); };
    auto put32 = [&](DWORD v) { for (int i = 0; i < 4; i++) w.push_back((BYTE)((v >> (8 * i)) & 0xFF)); };
    const char* riff = "RIFF"; w.insert(w.end(), riff, riff + 4);
    put32(36 + dataSize);
    const char* wave = "WAVEfmt "; w.insert(w.end(), wave, wave + 8);
    put32(16); put16(1); put16(1);
    put32((DWORD)rate);
    put32((DWORD)(rate * 2));
    put16(2); put16(16);
    const char* data = "data"; w.insert(w.end(), data, data + 4);
    put32(dataSize);
    for (int i = 0; i < n; i++) {
        int v = (int)(Clampf(smp[i], -1.f, 1.f) * 30000.f);
        put16(v);
    }
    return w;
}

static void BuildSounds() {
    const int rate = 22050;
    // 指针"哒"声
    {
        std::vector<float> s((size_t)(rate * 0.045));
        for (size_t i = 0; i < s.size(); i++) {
            float t = (float)i / rate;
            float env = expf(-t * 90.f);
            float body = sinf(2.f * 3.14159265f * 1500.f * t) * 0.6f +
                         sinf(2.f * 3.14159265f * 2600.f * t) * 0.3f;
            float click = ((float)((i * 2654435761u) % 2000) / 1000.f - 1.f) * expf(-t * 300.f) * 0.35f;
            s[i] = (body + click) * env * 0.55f;
        }
        g.sndTick = MakeWav(s, rate);
    }
    // 中奖音
    {
        float notes[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
        std::vector<float> s;
        for (int k = 0; k < 4; k++) {
            int len = (int)(rate * 0.16);
            for (int i = 0; i < len; i++) {
                float t = (float)i / rate;
                float env = expf(-t * 11.f) * (1.f - expf(-t * 400.f));
                float v = sinf(2.f * 3.14159265f * notes[k] * t) * 0.62f +
                          sinf(2.f * 3.14159265f * notes[k] * 2.f * t) * 0.22f;
                s.push_back(v * env * 0.34f);
            }
        }
        g.sndWin = MakeWav(s, rate);
    }
}

static void PlaySnd(std::vector<BYTE>& buf) {
    if (!g.soundOn || buf.empty()) return;
    PlaySoundW((LPCWSTR)buf.data(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// ============================ 字体 & 布局 =================================

static const wchar_t* PickFamily() {
    static const wchar_t* cands[] = { L"Microsoft YaHei UI", L"Microsoft YaHei", L"微软雅黑", L"Segoe UI" };
    for (auto c : cands) {
        FontFamily ff(c);
        if (ff.IsAvailable()) return c;
    }
    return L"Segoe UI";
}

static void BuildFonts(float S) {
    if (fabs(S - g.fontScale) < 0.01f && g.fRow) return;
    g.fontScale = S;
    g.layerDirty = true;
    delete g.fTitle; delete g.fSub; delete g.fRow; delete g.fSmall; delete g.fCol;
    delete g.fBtn; delete g.fHub; delete g.fToastName; delete g.fToastSmall; delete g.fBig;
    const wchar_t* fam = PickFamily();
    g.fTitle      = new Font(fam, 21.f * S, FontStyleBold, UnitPixel);
    g.fSub        = new Font(fam, 12.5f * S, FontStyleRegular, UnitPixel);
    g.fRow        = new Font(fam, 15.f * S, FontStyleRegular, UnitPixel);
    g.fSmall      = new Font(fam, 11.5f * S, FontStyleRegular, UnitPixel);
    g.fCol        = new Font(fam, 11.5f * S, FontStyleRegular, UnitPixel);
    g.fBtn        = new Font(fam, 14.f * S, FontStyleBold, UnitPixel);
    g.fHub        = new Font(fam, 15.f * S, FontStyleBold, UnitPixel);
    g.fToastName  = new Font(fam, 27.f * S, FontStyleBold, UnitPixel);
    g.fToastSmall = new Font(fam, 12.f * S, FontStyleRegular, UnitPixel);
    g.fBig        = new Font(fam, 34.f * S, FontStyleBold, UnitPixel);
}

static void ComputeLayout(float W, float H, float S) {
    g.L.S = S; g.L.W = W; g.L.H = H;
    float pad = 22.f * S;
    float panelW = Clampf(W * 0.345f, 330.f * S, 430.f * S);
    if (panelW > W * 0.5f) panelW = W * 0.46f;
    g.L.panel = RectF(W - pad - panelW, pad, panelW, H - 2 * pad);
    float gap = 26.f * S;
    g.L.wheelArea = RectF(pad, pad, (W - pad - panelW) - pad - gap, H - 2 * pad);

    float waW = g.L.wheelArea.Width, waH = g.L.wheelArea.Height;
    float Rv = (waH - (16.f + 52.f) * S) / 2.41f;   // 上方留指针空间，下方留提示文字
    float Rh = (waW - 16.f * S) / 2.30f;
    float R = (Rv < Rh ? Rv : Rh);
    if (R < 60.f * S) R = 60.f * S;
    g.L.R = R;
    g.L.wc = PointF(g.L.wheelArea.X + waW * 0.5f, g.L.wheelArea.Y + 16.f * S + R * 1.27f);
    g.L.hubR = R * 0.175f;

    float inX = g.L.panel.X + 18.f * S;
    float inW = g.L.panel.Width - 36.f * S;
    float btnH = 38.f * S;
    float btnY = g.L.panel.GetBottom() - 18.f * S - btnH;
    float bw = (inW - 16.f * S) / 3.f;
    g.L.btnAdd    = RectF(inX, btnY, bw, btnH);
    g.L.btnPreset = RectF(inX + bw + 8.f * S, btnY, bw, btnH);
    g.L.btnReset  = RectF(inX + 2 * (bw + 8.f * S), btnY, bw, btnH);
    g.L.btnMute   = RectF(g.L.wheelArea.X + 4.f * S,
                          g.L.wheelArea.GetBottom() - 40.f * S, 86.f * S, 32.f * S);

    g.L.listX = inX;
    g.L.listW = inW;
    g.L.listTop = g.L.panel.Y + 96.f * S;
    g.L.listBottom = btnY - 34.f * S;
    g.L.rowH = 44.f * S;
    g.L.rowGap = 6.f * S;
}

static RectF PanelRowRect(int i) {
    const Layout& L = g.L;
    return RectF(L.listX, L.listTop + i * (L.rowH + L.rowGap) - g.scroll, L.listW, L.rowH);
}

static float ListContentH() {
    int n = (int)g.items.size();
    if (n <= 0) return 0;
    return n * (g.L.rowH + g.L.rowGap) - g.L.rowGap;
}
static float MaxScroll() {
    float view = g.L.listBottom - g.L.listTop;
    return (std::max)(0.f, ListContentH() - view);
}
static void ClampScroll() { g.scroll = Clampf(g.scroll, 0.f, MaxScroll()); }

static int HitRow(PointF p) {
    const Layout& L = g.L;
    if (p.X < L.listX || p.X > L.listX + L.listW) return -1;
    if (p.Y < L.listTop || p.Y > L.listBottom) return -1;
    int n = (int)g.items.size();
    for (int i = 0; i < n; i++) {
        RectF r = PanelRowRect(i);
        if (p.Y >= r.Y && p.Y <= r.GetBottom()) return i;
        if (r.Y > L.listBottom) break;
    }
    return -1;
}

static int HitButton(PointF p) {
    const Layout& L = g.L;
    if (L.btnAdd.Contains(p)) return ButtonHit::ADD;
    if (L.btnPreset.Contains(p)) return ButtonHit::PRESET;
    if (L.btnReset.Contains(p)) return ButtonHit::RESET;
    if (L.btnMute.Contains(p)) return ButtonHit::MUTE;
    int row = HitRow(p);
    if (row >= 0) {
        RectF r = PanelRowRect(row);
        RectF del(r.GetRight() - 30.f * L.S, r.Y + 8.f * L.S, 24.f * L.S, r.Height - 16.f * L.S);
        if (p.X >= del.X) return ButtonHit::DELETE_BASE + row;
    }
    return ButtonHit::NONE;
}

// ============================ 转盘盘面绘制 =================================
// 在 2 倍超采样位图上绘制静止盘面（扇区 + 文字 + 金属外圈）

static void BuildWheel(float R) {
    const float SS = 2.0f;
    float Rout = R * 1.145f;
    int side = (int)ceilf(2.f * Rout * SS);
    if (side < 32) side = 32;
    Bitmap* bmp = new Bitmap(side, side, PixelFormat32bppPARGB);
    Graphics gr(bmp);
    gr.SetSmoothingMode(SmoothingModeAntiAlias);
    gr.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    gr.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    gr.SetCompositingQuality(CompositingQualityHighQuality);
    gr.TranslateTransform(Rout * SS, Rout * SS);
    gr.ScaleTransform(SS, SS);

    int n = (int)::g.items.size();
    double total = 0;
    for (auto& it : ::g.items) total += (it.w > 0 ? it.w : 0);
    if (n <= 0 || total <= 0) {
        // 空盘面
        gr.FillEllipse(&SolidBrush(RGBA(255, 255, 255, 10)), -R, -R, R * 2, R * 2);
        delete ::g.wheel;
        ::g.wheel = bmp;
        ::g.wheelBuiltR = R;
        ::g.wheelDirty = false;
        return;
    }

    // ---- 外圈光晕 ----
    GlowEllipse(gr, PointF(0, 0), R * 1.13f, R * 1.13f, RGBA(255, 196, 92, 60), RGBA(255, 180, 60, 0));

    // ---- 扇区 ----
    std::vector<float> startDeg(n), spanDeg(n);
    double cum = 0;
    for (int i = 0; i < n; i++) {
        startDeg[i] = (float)(360.0 * cum / total);
        cum += (::g.items[i].w > 0 ? ::g.items[i].w : 0);
        spanDeg[i] = (float)(360.0 * cum / total) - startDeg[i];
    }

    for (int i = 0; i < n; i++) {
        Color base = PaletteColor(i);
        GraphicsPath pie;
        RectF box(-R, -R, R * 2, R * 2);
        if (n == 1) {
            pie.AddEllipse(box);
        } else {
            pie.AddPie(box, startDeg[i] - 90.f, (spanDeg[i] > 0.01f ? spanDeg[i] : 0.01f));
        }
        // 由内到外的明暗渐变，让扇区有立体感
        PointF p1(cosf((startDeg[i] + spanDeg[i] * 0.5f - 90.f) * 3.14159265f / 180.f) * R * 0.05f,
                  sinf((startDeg[i] + spanDeg[i] * 0.5f - 90.f) * 3.14159265f / 180.f) * R * 0.05f);
        PointF p2(cosf((startDeg[i] + spanDeg[i] * 0.5f - 90.f) * 3.14159265f / 180.f) * R,
                  sinf((startDeg[i] + spanDeg[i] * 0.5f - 90.f) * 3.14159265f / 180.f) * R);
        LinearGradientBrush lg(p1, p2, MixColor(base, RGBA(255, 255, 255), 0.20f),
                               MixColor(base, RGBA(0, 0, 0), 0.16f));
        gr.FillPath(&lg, &pie);
    }

    // ---- 盘面高光 & 暗角（限制在圆内）----
    {
        GraphicsPath disc;
        disc.AddEllipse(-R * 0.985f, -R * 0.985f, R * 1.97f, R * 1.97f);
        GraphicsState st = gr.Save();
        gr.SetClip(&disc);
        GlowEllipse(gr, PointF(-R * 0.05f, -R * 0.05f), R * 0.95f, R * 0.95f,
                    RGBA(255, 255, 255, 46), RGBA(255, 255, 255, 0));
        GraphicsPath ringP;
        ringP.AddEllipse(-R, -R, R * 2, R * 2);
        PathGradientBrush vig(&ringP);
        vig.SetCenterPoint(PointF(0, 0));
        vig.SetCenterColor(RGBA(0, 0, 0, 0));
        Color sc[1] = { RGBA(8, 6, 20, 120) };
        int nn = 1;
        vig.SetSurroundColors(sc, &nn);
        vig.SetFocusScales(0.80f, 0.80f);
        gr.FillPath(&vig, &ringP);
        gr.Restore(st);
    }

    // ---- 分割线 ----
    if (n > 1) {
        Pen pen(RGBA(255, 255, 255, 150), R * 0.0065f);
        for (int i = 0; i < n; i++) {
            double a = (startDeg[i] - 90.0) * 3.14159265 / 180.0;
            gr.DrawLine(&pen, PointF(0, 0), PointF((REAL)(cos(a) * R * 0.99), (REAL)(sin(a) * R * 0.99)));
        }
    }

    // ---- 文字 ----
    const wchar_t* fam = PickFamily();
    for (int i = 0; i < n; i++) {
        if (spanDeg[i] < 1.0f) continue;
        float mid = startDeg[i] + spanDeg[i] * 0.5f;
        float R0 = R * 0.935f;
        float basePx = Clampf(R * 0.105f, 11.f, 30.f);
        // 依据扇区宽度自适应字号
        float fs = basePx;
        float maxLen = R0 - R * 0.26f;
        std::wstring label = ::g.items[i].name;
        for (int k = 0; k < 10; k++) {
            Font probe(fam, fs, FontStyleBold, UnitPixel);
            float tw = 0, th = 0;
            MeasureS(gr, label, &probe, &tw, &th);
            float innerR = R0 - (tw < maxLen ? tw : maxLen);
            float sliceW = (float)(2.0 * 3.14159265 * innerR / n);
            if (fs * 1.28f <= sliceW * 0.98f || fs <= 10.f) break;
            fs *= 0.92f;
        }
        Font fnt(fam, fs, FontStyleBold, UnitPixel);
        GraphicsState st = gr.Save();
        float avail = R0 - R * 0.24f;
        std::wstring shown = FitText(gr, label, &fnt, avail);
        float screenDeg = mid - 90.f;
        bool flip = cosf(screenDeg * 3.14159265f / 180.f) < 0.f;   // 左半边翻转，避免文字倒置
        gr.RotateTransform(flip ? screenDeg + 180.f : screenDeg);
        if (!flip) {
            RectF tr(R0 - avail - 2.f, -fs * 1.05f, avail + 2.f, fs * 2.1f);
            DrawTextS(gr, shown, &fnt, tr, RGBA(24, 26, 44, 235), StringAlignmentFar,
                      StringAlignmentCenter, false, 0.8f, 1.2f, RGBA(255, 255, 255, 105));
        } else {
            RectF tr(-R0, -fs * 1.05f, avail + 2.f, fs * 2.1f);
            DrawTextS(gr, shown, &fnt, tr, RGBA(24, 26, 44, 235), StringAlignmentNear,
                      StringAlignmentCenter, false, 0.8f, 1.2f, RGBA(255, 255, 255, 105));
        }
        gr.Restore(st);
    }

    // ---- 金属外圈 ----
    {
        float ro = R * 1.062f, ri = R * 0.965f;
        GraphicsPath ring;
        ring.AddEllipse(-ro, -ro, ro * 2, ro * 2);
        GraphicsPath inner;
        inner.AddEllipse(-ri, -ri, ri * 2, ri * 2);
        Region reg(&ring);
        reg.Exclude(&inner);
        LinearGradientBrush gb(PointF(-ro, -ro), PointF(ro, ro),
                               RGBA(255, 246, 214), RGBA(176, 126, 26));
        Color stops[3] = { RGBA(255, 249, 226), RGBA(198, 148, 40), RGBA(255, 236, 176) };
        REAL pos[3] = { 0.f, 0.5f, 1.f };
        gb.SetInterpolationColors(stops, pos, 3);
        gr.FillRegion(&gb, &reg);
        // 外圈描边
        Pen pOut(RGBA(120, 82, 12, 220), R * 0.006f);
        gr.DrawEllipse(&pOut, -ro, -ro, ro * 2, ro * 2);
        Pen pIn(RGBA(255, 255, 255, 70), R * 0.005f);
        gr.DrawEllipse(&pIn, -ri, -ri, ri * 2, ri * 2);
        // 顶部弧面高光
        Pen gloss(RGBA(255, 255, 255, 150), R * 0.016f);
        gloss.SetLineCap(LineCapRound, LineCapRound, DashCapRound);
        gr.DrawArc(&gloss, -ro * 0.955f, -ro * 0.955f, ro * 1.91f, ro * 1.91f, 190.f, 120.f);
        Pen dark(RGBA(80, 50, 0, 90), R * 0.012f);
        gr.DrawArc(&dark, -ro * 0.945f, -ro * 0.945f, ro * 1.89f, ro * 1.89f, 20.f, 130.f);
    }

    // ---- 灯泡 ----
    {
        int bulbs = (n <= 8 ? 24 : 32);
        float br = R * 0.031f;
        float rr = R * 1.0135f;
        for (int i = 0; i < bulbs; i++) {
            double a = (i * 360.0 / bulbs - 90.0) * 3.14159265 / 180.0;
            PointF c((REAL)(cos(a) * rr), (REAL)(sin(a) * rr));
            bool bright = (i % 2 == 0);
            GlowEllipse(gr, c, br * 3.2f, br * 3.2f,
                        bright ? RGBA(255, 244, 200, 130) : RGBA(255, 220, 150, 50), RGBA(255, 220, 150, 0));
            SolidBrush b(bright ? RGBA(255, 252, 236) : RGBA(198, 150, 44));
            gr.FillEllipse(&b, c.X - br, c.Y - br, br * 2, br * 2);
            SolidBrush hi(RGBA(255, 255, 255, bright ? 220 : 120));
            gr.FillEllipse(&hi, c.X - br * 0.45f, c.Y - br * 0.55f, br * 0.7f, br * 0.7f);
        }
    }

    // ---- 盘面内沿阴影 ----
    {
        Pen p(RGBA(60, 40, 0, 60), R * 0.02f);
        gr.DrawEllipse(&p, -R * 0.995f, -R * 0.995f, R * 1.99f, R * 1.99f);
    }

    delete ::g.wheel;
    ::g.wheel = bmp;
    ::g.wheelBuiltR = R;
    ::g.wheelDirty = false;
}

// ============================ 静态层（背景 + 面板）=========================

static void BuildLayer(int W, int H) {
    if (!g.layer || g.layer->GetWidth() != W || g.layer->GetHeight() != H) {
        delete g.layer;
        g.layer = new Bitmap(W, H, PixelFormat32bppPARGB);
    }
    const Layout& L = g.L;
    float S = L.S;
    Graphics g2(g.layer);
    g2.SetSmoothingMode(SmoothingModeAntiAlias);
    g2.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    g2.SetCompositingQuality(CompositingQualityHighQuality);

    // 背景渐变
    LinearGradientBrush bg(RectF(0, 0, (REAL)W, (REAL)H), RGBA(26, 30, 56), RGBA(8, 10, 20), 90.f, true);
    Color bgs[4] = { RGBA(32, 36, 66), RGBA(20, 23, 45), RGBA(13, 15, 30), RGBA(7, 8, 16) };
    REAL bgp[4] = { 0.f, 0.42f, 0.75f, 1.f };
    bg.SetInterpolationColors(bgs, bgp, 4);
    g2.FillRectangle(&bg, (REAL)0, (REAL)0, (REAL)W, (REAL)H);

    // 转盘后的光晕
    GlowEllipse(g2, PointF(L.wc.X, L.wc.Y), L.R * 1.75f, L.R * 1.75f,
                RGBA(96, 92, 220, 62), RGBA(90, 80, 220, 0));
    GlowEllipse(g2, PointF(L.wc.X, L.wc.Y + L.R * 0.1f), L.R * 1.28f, L.R * 1.28f,
                RGBA(255, 176, 72, 30), RGBA(255, 160, 60, 0));
    // 面板后暖光
    GlowEllipse(g2, PointF(L.panel.X + L.panel.Width * 0.5f, L.panel.GetBottom()),
                L.panel.Width * 0.95f, L.panel.Height * 0.45f,
                RGBA(120, 110, 255, 34), RGBA(120, 110, 255, 0));

    // ---- 面板 ----
    {
        RectF p = L.panel;
        GlowEllipse(g2, PointF(p.X + p.Width * 0.5f, p.Y + p.Height * 0.5f),
                    p.Width * 0.75f, p.Height * 0.62f, RGBA(0, 0, 0, 90), RGBA(0, 0, 0, 0));
        SolidBrush fill(RGBA(255, 255, 255, 16));
        FillRound(g2, p, 18.f * S, &fill);
        Pen edge(RGBA(255, 255, 255, 34), 1.f);
        StrokeRound(g2, p, 18.f * S, &edge);
        Pen top(RGBA(255, 255, 255, 60), 1.f);
        GraphicsPath tp;
        tp.AddArc(p.X + 2, p.Y + 1, 34 * S, 34 * S, 180, 90);
        tp.AddLine(p.X + 17 * S, p.Y + 1, p.GetRight() - 17 * S, p.Y + 1);
        g2.DrawPath(&top, &tp);
    }

    // 标题
    DrawTextS(g2, L"转盘设置", g.fTitle, RectF(L.panel.X + 18 * S, L.panel.Y + 16 * S, 200 * S, 30 * S),
              RGBA(240, 244, 255), StringAlignmentNear, StringAlignmentCenter);
    DrawTextS(g2, L"点击名称或权重即可修改，权重越大命中概率越高", g.fSub,
              RectF(L.panel.X + 18 * S, L.panel.Y + 48 * S, L.panel.Width - 36 * S, 20 * S),
              RGBA(150, 160, 195), StringAlignmentNear, StringAlignmentCenter);

    // 列标题
    {
        float y = L.listTop - 24 * S;
        float nameW = L.listW - 224 * S;
        DrawTextS(g2, L"名称", g.fCol, RectF(L.listX + 38 * S, y, nameW, 16 * S),
                  RGBA(130, 140, 175), StringAlignmentNear, StringAlignmentCenter);
        DrawTextS(g2, L"权重", g.fCol, RectF(L.listX + 212 * S, y, 70 * S, 16 * S),
                  RGBA(130, 140, 175), StringAlignmentCenter, StringAlignmentCenter);
        DrawTextS(g2, L"概率", g.fCol, RectF(L.listX + 254 * S, y, 106 * S, 16 * S),
                  RGBA(130, 140, 175), StringAlignmentFar, StringAlignmentCenter);
    }

    // ---- 列表 ----
    double total = 0;
    for (auto& it : g.items) total += (it.w > 0 ? it.w : 0);
    {
        GraphicsState st = g2.Save();
        g2.SetClip(RectF(L.listX - 4 * S, L.listTop - 2 * S, L.listW + 8 * S,
                         L.listBottom - L.listTop + 4 * S), CombineModeReplace);
        int n = (int)g.items.size();
        for (int i = 0; i < n; i++) {
            RectF r = PanelRowRect(i);
            if (r.GetBottom() < L.listTop - 2 * S || r.Y > L.listBottom + 2 * S) continue;
            bool selected = (i == g.sel);
            bool hovered = (i == g.hoverRow);
            Color pc = PaletteColor(i);

            // 行底
            if (selected || hovered) {
                SolidBrush rb(selected ? RGBA(255, 255, 255, 26) : RGBA(255, 255, 255, 14));
                FillRound(g2, r, 12 * S, &rb);
            }
            // 概率条
            double pct = (total > 0 ? (g.items[i].w > 0 ? g.items[i].w : 0) / total : 0);
            if (pct > 0.0005) {
                GraphicsState st2 = g2.Save();
                GraphicsPath rp;
                RoundRectPath(rp, r, 12 * S);
                g2.SetClip(&rp);
                float bw = (float)(r.Width * Clampd(pct, 0, 1));
                if (bw < 6 * S) bw = 6 * S;
                LinearGradientBrush bb(RectF(r.X, r.Y, bw, r.Height), Alpha(pc, selected ? 105 : 70),
                                       Alpha(pc, 8), 0.f, false);
                g2.FillRectangle(&bb, (REAL)r.X, (REAL)r.Y, (REAL)bw, (REAL)r.Height);
                g2.Restore(st2);
            }
            if (selected) {
                Pen sp(Alpha(pc, 210), 1.4f);
                StrokeRound(g2, r, 12 * S, &sp);
                SolidBrush bar(pc);
                FillRound(g2, RectF(r.X + 3 * S, r.Y + 9 * S, 3.4f * S, r.Height - 18 * S), 2 * S, &bar);
            }
            if (hovered && !selected) {
                Pen hp(RGBA(255, 255, 255, 40), 1.f);
                StrokeRound(g2, r, 12 * S, &hp);
            }

            // 色块
            RectF chipR(r.X + 13 * S, r.Y + r.Height * 0.5f - 8 * S, 16 * S, 16 * S);
            SolidBrush chip(pc);
            FillRound(g2, chipR, 5 * S, &chip);
            SolidBrush chipHi(RGBA(255, 255, 255, 90));
            FillRound(g2, RectF(chipR.X + 2 * S, chipR.Y + 2 * S, chipR.Width - 4 * S, chipR.Height * 0.42f),
                      3 * S, &chipHi);

            float nameX = r.X + 38 * S;
            float nameW = r.Width - 28 * S - 16 * S - 76 * S - 74 * S - 30 * S;
            bool nameHidden = (g.editRow == i && g.editField == 0);
            if (!nameHidden) {
                std::wstring t = FitText(g2, g.items[i].name, g.fRow, nameW);
                DrawTextS(g2, t, g.fRow, RectF(nameX, r.Y, nameW, r.Height),
                          selected ? RGBA(255, 255, 255) : RGBA(226, 232, 250),
                          StringAlignmentNear, StringAlignmentCenter);
            }

            // 权重胶囊
            float wBoxW = 66 * S;
            RectF wBox(nameX + nameW + 6 * S, r.Y + 8 * S, wBoxW, r.Height - 16 * S);
            bool wHidden = (g.editRow == i && g.editField == 1);
            SolidBrush wbg(wHidden ? RGBA(255, 255, 255, 30) : RGBA(255, 255, 255, 16));
            FillRound(g2, wBox, 8 * S, &wbg);
            Pen wpen(RGBA(255, 255, 255, wHidden ? 90 : 40), 1.f);
            StrokeRound(g2, wBox, 8 * S, &wpen);
            if (!wHidden) {
                DrawTextS(g2, FmtWeight(g.items[i].w), g.fRow, wBox,
                          RGBA(196, 206, 236), StringAlignmentCenter, StringAlignmentCenter);
            }

            // 概率
            std::wstring ps = FmtPct(pct * 100.0);
            float pctX = wBox.GetRight() + 4 * S;
            float pctW = r.GetRight() - 34 * S - pctX;
            if (pctW > 30 * S) {
                DrawTextS(g2, ps, g.fRow, RectF(pctX, r.Y, pctW, r.Height), RGBA(255, 209, 102),
                          StringAlignmentFar, StringAlignmentCenter);
            }

            // 删除按钮
            if (hovered || selected) {
                RectF del(r.GetRight() - 30 * S, r.Y + 8 * S, 24 * S, r.Height - 16 * S);
                bool dh = (g.hoverBtn == ButtonHit::DELETE_BASE + i);
                SolidBrush db(dh ? RGBA(255, 92, 92, 210) : RGBA(255, 255, 255, 20));
                FillRound(g2, del, 7 * S, &db);
                Font f(PickFamily(), 13 * S, FontStyleBold, UnitPixel);
                DrawTextS(g2, L"×", &f, del, dh ? RGBA(255, 255, 255) : RGBA(200, 208, 230),
                          StringAlignmentCenter, StringAlignmentCenter);
            }
        }
        g2.Restore(st);
    }

    // 列表底部分隔线 + 提示
    {
        Pen line(RGBA(255, 255, 255, 26), 1.f);
        g2.DrawLine(&line, L.listX, L.listBottom + 14 * S, L.listX + L.listW, L.listBottom + 14 * S);
        DrawTextS(g2, L"修改会自动保存，下次打开直接使用", g.fSmall,
                  RectF(L.listX, L.listBottom + 16 * S, L.listW, 22 * S), RGBA(126, 136, 172),
                  StringAlignmentNear, StringAlignmentCenter);
        if (!g.lastResult.empty()) {
            DrawTextS(g2, L"上次结果：" + g.lastResult, g.fSmall,
                      RectF(L.listX, L.listBottom + 16 * S, L.listW, 22 * S), RGBA(216, 186, 120),
                      StringAlignmentFar, StringAlignmentCenter);
        }
    }

    // ---- 按钮 ----
    struct BtnDef { RectF r; const wchar_t* label; int id; };
    BtnDef btns[3] = {
        { L.btnAdd, L"＋ 添加选项", ButtonHit::ADD },
        { L.btnPreset, L"随机示例", ButtonHit::PRESET },
        { L.btnReset, L"恢复默认", ButtonHit::RESET },
    };
    for (auto& b : btns) {
        bool h = (g.hoverBtn == b.id);
        SolidBrush fill(h ? RGBA(255, 255, 255, 40) : RGBA(255, 255, 255, 22));
        FillRound(g2, b.r, 11 * S, &fill);
        Pen pen(RGBA(255, 255, 255, h ? 90 : 46), 1.f);
        StrokeRound(g2, b.r, 11 * S, &pen);
        DrawTextS(g2, b.label, g.fBtn, b.r, h ? RGBA(255, 255, 255) : RGBA(214, 222, 244),
                  StringAlignmentCenter, StringAlignmentCenter);
    }

    // ---- 音效按钮 ----
    {
        RectF r = L.btnMute;
        bool h = (g.hoverBtn == ButtonHit::MUTE);
        SolidBrush fill(h ? RGBA(255, 255, 255, 34) : RGBA(255, 255, 255, 16));
        FillRound(g2, r, r.Height * 0.5f, &fill);
        Pen pen(RGBA(255, 255, 255, h ? 80 : 36), 1.f);
        StrokeRound(g2, r, r.Height * 0.5f, &pen);
        std::wstring t = g.soundOn ? L"♪ 音效 开" : L"♪ 音效 关";
        DrawTextS(g2, t, g.fSmall, r, g.soundOn ? RGBA(214, 224, 250) : RGBA(140, 148, 176),
                  StringAlignmentCenter, StringAlignmentCenter);
    }

    // ---- 底部提示 ----
    if (!g.shotMode) {
        DrawTextS(g2, L"空格键 / 点击转盘开始旋转　·　滚轮滚动列表　·　Delete 删除选中项",
                  g.fSmall, RectF(L.wheelArea.X + 8 * S, L.wheelArea.GetBottom() - 34 * S,
                                  L.wheelArea.Width - 100 * S, 22 * S),
                  RGBA(120, 130, 165), StringAlignmentCenter, StringAlignmentCenter);
    }

    g.layerDirty = false;
}

// ============================ 动画状态 ====================================

static double EaseOutQuart(double p) {
    double q = 1.0 - Clampd(p, 0, 1);
    return 1.0 - q * q * q * q;
}

static void EnsureWheel() {
    float R = g.L.R;
    if (!g.wheel || g.wheelDirty) { BuildWheel(R); return; }
    if (fabs(R - g.wheelBuiltR) > 0.6f && (NowSec() - g.lastResizeT) > 0.20) BuildWheel(R);
}

static double TotalWeight() {
    double t = 0;
    for (auto& it : g.items) t += (it.w > 0 ? it.w : 0);
    return t;
}

static void SliceAngles(int idx, double* startDeg, double* spanDeg) {
    double total = TotalWeight();
    if (total <= 0 || idx < 0 || idx >= (int)g.items.size()) {
        *startDeg = 0; *spanDeg = 360; return;
    }
    double cum = 0;
    for (int i = 0; i < idx; i++) cum += (g.items[i].w > 0 ? g.items[i].w : 0);
    *startDeg = 360.0 * cum / total;
    *spanDeg = 360.0 * (g.items[idx].w > 0 ? g.items[idx].w : 0) / total;
}

static int IndexAtLocal(double localDeg) {
    double total = TotalWeight();
    if (total <= 0) return -1;
    double acc = 0;
    for (size_t i = 0; i < g.items.size(); i++) {
        acc += 360.0 * (g.items[i].w > 0 ? g.items[i].w : 0) / total;
        if (localDeg < acc) return (int)i;
    }
    return (int)g.items.size() - 1;
}

static void SpawnConfetti(int count) {
    static const int cols[7][3] = {
        {255, 209, 102}, {255, 107, 107}, { 34, 184, 207}, {177, 151, 252},
        {148, 216,  45}, {255, 255, 255}, {247, 131, 172},
    };
    std::uniform_real_distribution<float> u(0.f, 1.f);
    for (int i = 0; i < count; i++) {
        Particle p;
        float ang = u(g.rng) * 6.2831853f;
        float spd = 150.f + u(g.rng) * 640.f;
        p.x = g.L.wc.X + cosf(ang) * g.L.R * 0.22f;
        p.y = g.L.wc.Y + sinf(ang) * g.L.R * 0.22f - g.L.R * 0.10f;
        p.vx = cosf(ang) * spd;
        p.vy = sinf(ang) * spd - 420.f;
        p.rot = u(g.rng) * 6.2831853f;
        p.vrot = (u(g.rng) - 0.5f) * 16.f;
        p.size = (4.f + u(g.rng) * 7.f) * g.L.S;
        p.maxLife = 1.5f + u(g.rng) * 1.6f;
        p.life = p.maxLife;
        int ci = (int)(u(g.rng) * 7.f) % 7;
        p.col = RGBA(cols[ci][0], cols[ci][1], cols[ci][2]);
        g.parts.push_back(p);
    }
}

static void AdvanceConfetti(float dt) {
    for (auto& p : g.parts) {
        p.vy += 1450.f * dt;
        float damp = expf(-1.1f * dt);
        p.vx *= damp;
        p.vy *= damp;
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.rot += p.vrot * dt;
        p.life -= dt;
    }
    g.parts.erase(std::remove_if(g.parts.begin(), g.parts.end(),
                                 [](const Particle& p) { return p.life <= 0; }),
                  g.parts.end());
}

static void StartSpin() {
    if (g.spinning || g.items.empty()) return;
    double total = TotalWeight();
    if (total <= 0) return;
    std::uniform_real_distribution<double> u(0.0, 1.0);
    double r = u(g.rng) * total, acc = 0;
    int idx = (int)g.items.size() - 1;
    for (size_t i = 0; i < g.items.size(); i++) {
        acc += (g.items[i].w > 0 ? g.items[i].w : 0);
        if (r <= acc) { idx = (int)i; break; }
    }
    double st = 0, sp = 0;
    SliceAngles(idx, &st, &sp);
    double phi = st + sp * (0.16 + 0.68 * u(g.rng));
    double target = fmod(-phi, 360.0);
    if (target < 0) target += 360.0;
    double startMod = fmod(g.rot, 360.0);
    if (startMod < 0) startMod += 360.0;
    double deltaMod = target - startMod;
    if (deltaMod <= 0.5) deltaMod += 360.0;
    int turns = 5 + (int)(u(g.rng) * 3.0);
    g.rot0 = g.rot;
    g.rotDelta = 360.0 * turns + deltaMod;
    g.spinT0 = NowSec();
    g.spinDur = 4.1 + 0.9 * u(g.rng);
    g.spinning = true;
    g.pendingWinner = idx;
    g.winner = -1;
    g.resultT = 0;
    g.parts.clear();
    g.lastTickIdx = -1;
    g.lastTickT = 0;
}

static bool UpdateAnim(double dt, double now) {
    bool need = false;
    if (g.spinning) {
        double p = (now - g.spinT0) / g.spinDur;
        if (p >= 1.0) {
            g.rot = g.rot0 + g.rotDelta;
            g.spinning = false;
            g.angSpeed = 0;
            g.winner = g.pendingWinner;
            g.resultT = 0.0001;
            g.parts.clear();
            SpawnConfetti(170);
            if (g.winner >= 0 && g.winner < (int)g.items.size()) g.lastResult = g.items[g.winner].name;
            g.layerDirty = true;
            PlaySnd(g.sndWin);
        } else {
            g.rot = g.rot0 + g.rotDelta * EaseOutQuart(p);
            double q = 1.0 - p;
            g.angSpeed = g.rotDelta * 4.0 * q * q * q / g.spinDur;
            double local = fmod(-g.rot, 360.0);
            if (local < 0) local += 360.0;
            int idx = IndexAtLocal(local);
            if (idx != g.lastTickIdx) {
                g.lastTickIdx = idx;
                if (g.angSpeed > 20.0 && now - g.lastTickT > 0.032) {
                    g.lastTickT = now;
                    PlaySnd(g.sndTick);
                }
            }
        }
        need = true;
    }
    // 指针弹簧
    {
        float target = 11.f * Clampf((float)(g.angSpeed / 2600.0), 0.f, 1.f);
        g.wobV += (target - g.wob) * 900.f * (float)dt;
        g.wobV *= (float)exp(-8.0 * dt);
        g.wob += g.wobV * (float)dt;
        if (fabs(g.wob) > 0.02f || g.spinning) need = true;
    }
    if (!g.parts.empty()) { AdvanceConfetti((float)dt); need = true; }
    if (g.resultT > 0) {
        g.resultT += dt;
        if (g.resultT > 6.4) g.resultT = 0;
        need = true;
    }
    return need;
}

// ============================ 前景绘制 ====================================

static void DrawWheel(Graphics& g2) {
    const Layout& L = g.L;
    float R = L.R;
    // 盘下阴影
    GlowEllipse(g2, PointF(L.wc.X, L.wc.Y + R * 0.12f), R * 1.06f, R * 1.0f,
                RGBA(0, 0, 0, 165), RGBA(0, 0, 0, 0));
    if (!g.wheel) return;
    float drawR = R * 1.145f;
    bool fast = g.angSpeed > 420.0;
    g2.SetInterpolationMode(fast ? InterpolationModeBilinear : InterpolationModeHighQualityBicubic);
    g2.SetPixelOffsetMode(PixelOffsetModeHighQuality);
    g2.SetCompositingQuality(CompositingQualityHighSpeed);
    GraphicsState st = g2.Save();
    g2.TranslateTransform(L.wc.X, L.wc.Y);
    g2.RotateTransform((REAL)g.rot);
    g2.DrawImage(g.wheel, -drawR, -drawR, drawR * 2, drawR * 2);
    g2.Restore(st);
}

static void DrawWinnerGlow(Graphics& g2, double now) {
    if (g.resultT <= 0 || g.winner < 0 || g.winner >= (int)g.items.size()) return;
    const Layout& L = g.L;
    float R = L.R;
    double st0 = 0, sp0 = 0;
    SliceAngles(g.winner, &st0, &sp0);
    float pulse = 0.5f + 0.5f * (float)sin(now * 6.0);
    GraphicsState st = g2.Save();
    g2.TranslateTransform(L.wc.X, L.wc.Y);
    g2.RotateTransform((REAL)g.rot);
    GraphicsPath pie;
    RectF box(-R, -R, R * 2, R * 2);
    pie.AddPie(box, (REAL)(st0 - 90.0), (REAL)(sp0 > 0.02 ? sp0 : 0.02));
    SolidBrush b(RGBA(255, 255, 255, (int)(32 + 30 * pulse)));
    g2.FillPath(&b, &pie);
    Pen p(RGBA(255, 252, 220, (int)(140 + 90 * pulse)), R * 0.013f);
    g2.DrawPath(&p, &pie);
    g2.Restore(st);
}

static void DrawGlass(Graphics& g2) {
    const Layout& L = g.L;
    float R = L.R;
    GraphicsPath clip;
    clip.AddEllipse(L.wc.X - R, L.wc.Y - R, R * 2, R * 2);
    GraphicsState st = g2.Save();
    g2.SetClip(&clip);
    GlowEllipse(g2, PointF(L.wc.X - R * 0.33f, L.wc.Y - R * 0.44f), R * 0.66f, R * 0.44f,
                RGBA(255, 255, 255, 32), RGBA(255, 255, 255, 0));
    GlowEllipse(g2, PointF(L.wc.X, L.wc.Y + R * 0.95f), R * 0.95f, R * 0.34f,
                RGBA(0, 0, 0, 80), RGBA(0, 0, 0, 0));
    g2.Restore(st);
}

static void DrawHub(Graphics& g2) {
    const Layout& L = g.L;
    float hr = L.hubR;
    bool hot = g.hoverWheel;
    GlowEllipse(g2, L.wc, hr * 2.3f, hr * 2.3f,
                hot ? RGBA(255, 216, 130, 80) : RGBA(255, 216, 130, 42), RGBA(255, 216, 130, 0));
    float ro = hr * 1.28f;
    {
        GraphicsPath outer, inner;
        outer.AddEllipse(L.wc.X - ro, L.wc.Y - ro, ro * 2, ro * 2);
        inner.AddEllipse(L.wc.X - hr, L.wc.Y - hr, hr * 2, hr * 2);
        Region reg(&outer);
        reg.Exclude(&inner);
        LinearGradientBrush gold(PointF(L.wc.X - ro, L.wc.Y - ro), PointF(L.wc.X + ro, L.wc.Y + ro),
                                 RGBA(255, 250, 226), RGBA(184, 132, 30));
        g2.FillRegion(&gold, &reg);
    }
    {
        GraphicsPath disc;
        disc.AddEllipse(L.wc.X - hr, L.wc.Y - hr, hr * 2, hr * 2);
        PathGradientBrush pg(&disc);
        pg.SetCenterPoint(PointF(L.wc.X - hr * 0.2f, L.wc.Y - hr * 0.25f));
        pg.SetCenterColor(RGBA(58, 66, 102));
        Color sc[1] = { RGBA(10, 12, 24) };
        int n = 1;
        pg.SetSurroundColors(sc, &n);
        pg.SetFocusScales(0.25f, 0.25f);
        g2.FillPath(&pg, &disc);
        Pen edge(RGBA(255, 226, 160, 130), hr * 0.045f);
        g2.DrawEllipse(&edge, L.wc.X - hr, L.wc.Y - hr, hr * 2, hr * 2);
    }
    // 播放三角
    {
        float w = hr * 0.40f, h = hr * 0.46f;
        PointF pts[3] = {
            PointF(L.wc.X - w * 0.42f + hr * 0.10f, L.wc.Y - h),
            PointF(L.wc.X - w * 0.42f + hr * 0.10f, L.wc.Y + h),
            PointF(L.wc.X + w * 1.15f + hr * 0.10f, L.wc.Y),
        };
        GraphicsPath tri;
        tri.AddPolygon(pts, 3);
        LinearGradientBrush tg(PointF(L.wc.X, L.wc.Y - h), PointF(L.wc.X, L.wc.Y + h),
                               RGBA(255, 252, 232), RGBA(255, 199, 84));
        g2.FillPath(&tg, &tri);
    }
}

static void DrawPointer(Graphics& g2) {
    const Layout& L = g.L;
    float R = L.R;
    float baseY = L.wc.Y - R * 1.18f;
    float h = R * 0.285f;
    float w = R * 0.125f;
    GlowEllipse(g2, PointF(L.wc.X, baseY + h * 0.45f), w * 1.5f, h * 0.95f,
                RGBA(0, 0, 0, 100), RGBA(0, 0, 0, 0));
    GraphicsState st = g2.Save();
    g2.TranslateTransform(L.wc.X, baseY);
    g2.RotateTransform(g.wob);
    PointF pts[3] = { PointF(-w * 0.5f, 0), PointF(w * 0.5f, 0), PointF(0, h) };
    GraphicsPath tri;
    tri.AddPolygon(pts, 3);
    LinearGradientBrush lg(PointF(-w * 0.5f, 0), PointF(w * 0.5f, h),
                           RGBA(255, 249, 226), RGBA(190, 138, 34));
    g2.FillPath(&lg, &tri);
    Pen p(RGBA(255, 255, 255, 170), 1.6f * L.S);
    g2.DrawPath(&p, &tri);
    // 底座圆头
    float bw = w * 0.72f;
    LinearGradientBrush bg(PointF(-bw, -bw * 0.6f), PointF(bw, bw * 0.6f),
                           RGBA(255, 252, 236), RGBA(214, 162, 52));
    g2.FillEllipse(&bg, -bw, -bw * 0.62f, bw * 2, bw * 1.24f);
    g2.Restore(st);
}

static void DrawConfetti(Graphics& g2) {
    for (auto& p : g.parts) {
        float a = Clampf(p.life / 0.65f, 0.f, 1.f);
        SolidBrush b(Alpha(p.col, (int)(230 * a)));
        GraphicsState st = g2.Save();
        g2.TranslateTransform(p.x, p.y);
        g2.RotateTransform(p.rot * 57.2958f);
        g2.FillRectangle(&b, -p.size * 0.5f, -p.size * 0.34f, p.size, p.size * 0.68f);
        g2.Restore(st);
    }
}

static void DrawToast(Graphics& g2) {
    if (g.resultT <= 0 || g.winner < 0 || g.winner >= (int)g.items.size()) return;
    const Layout& L = g.L;
    float S = L.S;
    double t = g.resultT;
    float appear = (float)Clampd(t / 0.40, 0, 1);
    appear = 1.f - powf(1.f - appear, 3.f);
    float fade = (float)Clampd((6.4 - t) / 0.7, 0, 1);
    int A = (int)(255 * appear * fade);
    if (A <= 2) return;

    std::wstring nm = g.items[g.winner].name;
    float tw = 0, th = 0;
    MeasureS(g2, nm, g.fToastName, &tw, &th);
    float pw = (std::max)(190.f * S, tw + 96.f * S);
    float ph = 74.f * S;
    float px = L.wc.X - pw * 0.5f;
    float py = L.wc.Y - L.R * 0.44f - ph * 0.5f + (1.f - appear) * 14.f * S;
    RectF box(px, py, pw, ph);

    GlowEllipse(g2, PointF(box.X + box.Width * 0.5f, box.Y + box.Height * 0.5f),
                box.Width * 0.78f, box.Height * 1.35f, RGBA(0, 0, 0, (int)(130 * appear * fade)),
                RGBA(0, 0, 0, 0));
    GlowEllipse(g2, PointF(box.X + box.Width * 0.5f, box.Y + box.Height * 0.5f),
                box.Width * 0.62f, box.Height * 0.95f, RGBA(255, 196, 90, (int)(70 * appear * fade)),
                RGBA(255, 196, 90, 0));
    SolidBrush fill(RGBA(24, 27, 46, (int)(238 * appear * fade)));
    FillRound(g2, box, 16 * S, &fill);
    LinearGradientBrush borderBrush(RectF(box.X, box.Y, box.Width, box.Height),
                                    RGBA(255, 236, 180, A), RGBA(206, 152, 40, A), 30.f, false);
    Pen border(&borderBrush, 1.6f);
    StrokeRound(g2, box, 16 * S, &border);
    Pen gloss(RGBA(255, 255, 255, (int)(70 * appear * fade)), 1.f);
    GraphicsPath tp;
    tp.AddArc(box.X + 2, box.Y + 1, 30 * S, 30 * S, 180, 90);
    tp.AddLine(box.X + 15 * S, box.Y + 1, box.GetRight() - 15 * S, box.Y + 1);
    g2.DrawPath(&gloss, &tp);

    DrawTextS(g2, L"★ 本次结果", g.fToastSmall,
              RectF(box.X, box.Y + 9 * S, box.Width, 18 * S),
              RGBA(255, 213, 120, A), StringAlignmentCenter, StringAlignmentCenter);
    DrawTextS(g2, FitText(g2, nm, g.fToastName, box.Width - 36 * S), g.fToastName,
              RectF(box.X, box.Y + 26 * S, box.Width, 40 * S),
              RGBA(255, 255, 255, A), StringAlignmentCenter, StringAlignmentCenter);
}

static void RenderFrame(Graphics& g2, int W, int H) {
    const Layout& L = g.L;
    double now = NowSec();
    g2.SetInterpolationMode(InterpolationModeNearestNeighbor);
    g2.SetPixelOffsetMode(PixelOffsetModeHalf);
    g2.SetCompositingQuality(CompositingQualityHighSpeed);
    if (g.layer) {
        g2.DrawImage(g.layer, RectF(0, 0, (REAL)W, (REAL)H), 0, 0, (REAL)W, (REAL)H, UnitPixel);
    }
    if (g.items.empty()) {
        DrawTextS(g2, L"还没有选项", g.fBig, RectF(L.wheelArea.X, L.wc.Y - 40 * L.S, L.wheelArea.Width, 40 * L.S),
                  RGBA(255, 255, 255, 120), StringAlignmentCenter, StringAlignmentCenter);
        DrawTextS(g2, L"点击右侧「＋ 添加选项」开始设置", g.fRow,
                  RectF(L.wheelArea.X, L.wc.Y + 4 * L.S, L.wheelArea.Width, 30 * L.S),
                  RGBA(170, 180, 210, 190), StringAlignmentCenter, StringAlignmentCenter);
        return;
    }
    DrawWheel(g2);
    DrawWinnerGlow(g2, now);
    DrawGlass(g2);
    DrawHub(g2);
    DrawPointer(g2);
    DrawConfetti(g2);
    DrawToast(g2);
}

// ============================ 行内编辑框 ==================================

#define WM_COMMITEDIT (WM_APP + 1)

static float g_dpi = 1.f;

static void RowCells(RectF r, RectF* nameC, RectF* wC, RectF* pctC, RectF* delC) {
    float S = g.L.S;
    float nameX = r.X + 38 * S;
    float nameW = r.Width - 28 * S - 16 * S - 76 * S - 74 * S - 30 * S;
    if (nameW < 20 * S) nameW = 20 * S;
    if (nameC) *nameC = RectF(nameX, r.Y + 6 * S, nameW, r.Height - 12 * S);
    RectF w(nameX + nameW + 6 * S, r.Y + 8 * S, 66 * S, r.Height - 16 * S);
    if (wC) *wC = w;
    if (pctC) *pctC = RectF(w.GetRight() + 4 * S, r.Y, r.GetRight() - 34 * S - (w.GetRight() + 4 * S), r.Height);
    if (delC) *delC = RectF(r.GetRight() - 30 * S, r.Y + 8 * S, 24 * S, r.Height - 16 * S);
}

static void EnsureEditFont() {
    float px = 15.f * g.L.S;
    if (g.hFontUI && fabs(px - g.editFontPx) < 0.5f) return;
    if (g.hFontUI) DeleteObject(g.hFontUI);
    g.editFontPx = px;
    g.hFontUI = CreateFontW(-(int)(px + 0.5f), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, PickFamily());
}

static WNDPROC g_oldEditProc = nullptr;
static int g_editGen = 0;

static LRESULT CALLBACK EditSubProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    if (m == WM_KEYDOWN) {
        if (wp == VK_RETURN) { PostMessageW(g.hwnd, WM_COMMITEDIT, (WPARAM)g_editGen, 1); return 0; }
        if (wp == VK_ESCAPE) { PostMessageW(g.hwnd, WM_COMMITEDIT, (WPARAM)g_editGen, 0); return 0; }
    }
    if (m == WM_CHAR && (wp == L'\r' || wp == 27 || wp == L'\n')) return 0;
    if (m == WM_KILLFOCUS) PostMessageW(g.hwnd, WM_COMMITEDIT, (WPARAM)g_editGen, 1);
    return CallWindowProcW(g_oldEditProc, h, m, wp, lp);
}

static void EndEdit(bool commit) {
    if (!g.hEdit) { g.editRow = -1; g.editField = -1; return; }
    int row = g.editRow, field = g.editField;
    wchar_t buf[512] = { 0 };
    if (commit) GetWindowTextW(g.hEdit, buf, 500);
    DestroyWindow(g.hEdit);
    g.hEdit = nullptr;
    g.editRow = -1;
    g.editField = -1;
    if (commit && row >= 0 && row < (int)g.items.size()) {
        std::wstring t = TrimW(buf);
        bool changed = false;
        if (field == 0) {
            if (!t.empty() && t != g.items[row].name) {
                if (t.size() > 24) t = t.substr(0, 24);
                g.items[row].name = t;
                changed = true;
            }
        } else {
            std::wstring s = t;
            if (!s.empty() && s[s.size() - 1] == L'%') s = TrimW(s.substr(0, s.size() - 1));
            wchar_t* e = nullptr;
            double v = wcstod(s.c_str(), &e);
            if (e && e != s.c_str() && v > 0) {
                v = Clampd(v, 0.0001, 1e7);
                if (fabs(v - g.items[row].w) > 1e-9) { g.items[row].w = v; changed = true; }
            }
        }
        if (changed) { g.wheelDirty = true; SaveConfig(); }
    }
    g.layerDirty = true;
    if (g.hwnd) {
        SetFocus(g.hwnd);
        InvalidateRect(g.hwnd, nullptr, FALSE);
    }
}

static void BeginEdit(int row, int field) {
    if (row < 0 || row >= (int)g.items.size()) return;
    if (g.hEdit) EndEdit(true);
    g_editGen++;
    RectF r = PanelRowRect(row);
    RectF nameC, wC;
    RowCells(r, &nameC, &wC, nullptr, nullptr);
    RectF box = (field == 0) ? nameC : wC;
    g.editRow = row;
    g.editField = field;
    EnsureEditFont();
    g.hEdit = CreateWindowExW(0, L"EDIT", L"",
                              WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
                              (int)box.X, (int)box.Y, (int)box.Width, (int)box.Height,
                              g.hwnd, nullptr, nullptr, nullptr);
    if (!g.hEdit) { g.editRow = -1; g.editField = -1; return; }
    SendMessageW(g.hEdit, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(g.hEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(6, 4));
    SetFocus(g.hEdit);
    SetWindowTextW(g.hEdit, field == 0 ? g.items[row].name.c_str() : FmtWeight(g.items[row].w).c_str());
    SendMessageW(g.hEdit, EM_SETSEL, 0, -1);
    g_oldEditProc = (WNDPROC)SetWindowLongPtrW(g.hEdit, GWLP_WNDPROC, (LONG_PTR)EditSubProc);
    SendMessageW(g.hEdit, EM_SETSEL, 0, -1);
    g.layerDirty = true;
    InvalidateRect(g.hwnd, nullptr, FALSE);
}

// ============================ 界面动作 ====================================

static void AddItemAction() {
    if ((int)g.items.size() >= 60) return;
    Item it;
    it.name = L"新选项";
    it.w = 1.0;
    g.items.push_back(it);
    g.sel = (int)g.items.size() - 1;
    g.scroll = MaxScroll();
    g.wheelDirty = true;
    g.layerDirty = true;
    SaveConfig();
    BeginEdit(g.sel, 0);
}

static void RemoveItemAction(int i) {
    if (i < 0 || i >= (int)g.items.size()) return;
    if (g.editRow == i) EndEdit(false);
    bool wasWinner = (g.winner == i);
    g.items.erase(g.items.begin() + i);
    if (g.sel >= (int)g.items.size()) g.sel = (int)g.items.size() - 1;
    if (wasWinner) { g.winner = -1; g.resultT = 0; }
    else if (g.winner > i) g.winner--;
    g.wheelDirty = true;
    g.layerDirty = true;
    ClampScroll();
    SaveConfig();
}

// ============================ 渲染与消息 ==================================

static void EnsureCaches(int W, int H) {
    ComputeLayout((float)W, (float)H, g_dpi);
    BuildFonts(g.L.S);
    EnsureWheel();
    if (g.layerDirty || !g.layer || g.layer->GetWidth() != W || g.layer->GetHeight() != H)
        BuildLayer(W, H);
}

static void PaintWindow(HWND hwnd) {
    RECT rc;
    GetClientRect(hwnd, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    if (W <= 0 || H <= 0) return;
    EnsureCaches(W, H);
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ old = SelectObject(mem, bmp);
    {
        Graphics g2(mem);
        g2.SetSmoothingMode(SmoothingModeAntiAlias);
        g2.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        RenderFrame(g2, W, H);
    }
    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void SetCursorFor(PointF p) {
    int b = HitButton(p);
    bool hand = (b != ButtonHit::NONE) || HitRow(p) >= 0;
    if (!hand) {
        float dx = p.X - g.L.wc.X, dy = p.Y - g.L.wc.Y;
        float R = g.L.R * 1.10f;
        if (dx * dx + dy * dy <= R * R) hand = true;
    }
    SetCursor(LoadCursorW(nullptr, hand ? IDC_HAND : IDC_ARROW));
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        {
            BOOL dark = TRUE;
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        }
        SetTimer(hwnd, 1, 15, nullptr);
        g.lastFrameT = NowSec();
        return 0;

    case WM_SIZE:
        g.lastResizeT = NowSec();
        g.layerDirty = true;
        if (g.hEdit) EndEdit(true);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = (MINMAXINFO*)lp;
        mm->ptMinTrackSize.x = (LONG)(940 * g_dpi);
        mm->ptMinTrackSize.y = (LONG)(640 * g_dpi);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            if (g.L.S > 0) SetCursorFor(PointF((REAL)pt.x, (REAL)pt.y));
            return TRUE;
        }
        break;

    case WM_PAINT:
        PaintWindow(hwnd);
        return 0;

    case WM_CTLCOLOREDIT: {
        HDC hdc = (HDC)wp;
        SetTextColor(hdc, RGB(236, 241, 255));
        SetBkColor(hdc, RGB(40, 45, 72));
        if (!g.editBrush) g.editBrush = CreateSolidBrush(RGB(40, 45, 72));
        return (LRESULT)g.editBrush;
    }

    case WM_TIMER: {
        double now = NowSec();
        double dt = now - g.lastFrameT;
        g.lastFrameT = now;
        if (dt > 0.12) dt = 0.12;
        bool need = UpdateAnim(dt, now);
        if (g.wheelDirty) need = true;
        if (fabs(g.L.R - g.wheelBuiltR) > 0.6f && (now - g.lastResizeT) > 0.20) need = true;
        if (need) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE: {
        PointF p((REAL)GET_X_LPARAM(lp), (REAL)GET_Y_LPARAM(lp));
        if (g.L.S <= 0) return 0;
        int btn = HitButton(p);
        int row = HitRow(p);
        bool hw = false;
        {
            float dx = p.X - g.L.wc.X, dy = p.Y - g.L.wc.Y, R = g.L.R * 1.10f;
            hw = (dx * dx + dy * dy) <= R * R;
        }
        if (btn != g.hoverBtn || row != g.hoverRow) {
            g.hoverBtn = btn;
            g.hoverRow = row;
            g.layerDirty = true;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        if (hw != g.hoverWheel) {
            g.hoverWheel = hw;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        SetCursorFor(p);
        return 0;
    }

    case WM_MOUSELEAVE:
        g.hoverBtn = ButtonHit::NONE;
        g.hoverRow = -1;
        g.hoverWheel = false;
        g.layerDirty = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN: {
        if (g.hEdit) EndEdit(true);
        PointF p((REAL)GET_X_LPARAM(lp), (REAL)GET_Y_LPARAM(lp));
        if (g.L.S <= 0) return 0;
        int btn = HitButton(p);
        if (btn == ButtonHit::ADD) { AddItemAction(); return 0; }
        if (btn == ButtonHit::PRESET) {
            int idx = std::uniform_int_distribution<int>(0, kPresetCount - 1)(g.rng);
            LoadPreset(idx);
            SaveConfig();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (btn == ButtonHit::RESET) {
            LoadPreset(0);
            SaveConfig();
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (btn == ButtonHit::MUTE) {
            g.soundOn = !g.soundOn;
            g.layerDirty = true;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (btn >= ButtonHit::DELETE_BASE) { RemoveItemAction(btn - ButtonHit::DELETE_BASE); return 0; }
        int row = HitRow(p);
        if (row >= 0) {
            g.sel = row;
            g.layerDirty = true;
            RectF r = PanelRowRect(row);
            RectF nameC, wC;
            RowCells(r, &nameC, &wC, nullptr, nullptr);
            if (p.X >= wC.X - 2 && p.X <= wC.GetRight() + 2) BeginEdit(row, 1);
            else if (p.X >= nameC.X - 6 && p.X <= nameC.GetRight() + 4) BeginEdit(row, 0);
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        float dx = p.X - g.L.wc.X, dy = p.Y - g.L.wc.Y, R = g.L.R * 1.10f;
        if (dx * dx + dy * dy <= R * R) StartSpin();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_MOUSEWHEEL: {
        if (g.L.S <= 0) return 0;
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        g.scroll -= (d / 120.f) * 70.f * g.L.S;
        ClampScroll();
        g.layerDirty = true;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (g.hEdit) return 0;
        if (wp == VK_SPACE) { StartSpin(); return 0; }
        if (wp == VK_RETURN) { StartSpin(); return 0; }
        if (wp == 'A') { AddItemAction(); return 0; }
        if (wp == VK_DELETE && g.sel >= 0) { RemoveItemAction(g.sel); return 0; }
        if (wp == VK_DOWN) { g.sel = Clampi(g.sel + 1, 0, (int)g.items.size() - 1); g.layerDirty = true; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        if (wp == VK_UP) { g.sel = Clampi(g.sel - 1, 0, (int)g.items.size() - 1); g.layerDirty = true; InvalidateRect(hwnd, nullptr, FALSE); return 0; }
        return 0;
    }

    case WM_COMMITEDIT:
        if ((int)wp == g_editGen) {
            EndEdit(lp == 1);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_DPICHANGED: {
        g_dpi = (float)HIWORD(wp) / 96.f;
        RECT* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        g.layerDirty = true;
        g.wheelDirty = true;
        if (g.hFontUI) { DeleteObject(g.hFontUI); g.hFontUI = nullptr; g.editFontPx = 0; }
        if (g.editBrush) { DeleteObject(g.editBrush); g.editBrush = nullptr; }
        BuildFonts(g_dpi);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ============================ 截图 / 预览（开发用）========================

static bool GetEncoderClsid(const wchar_t* mime, CLSID* out) {
    UINT num = 0, size = 0;
    if (GetImageEncodersSize(&num, &size) != Ok || size == 0) return false;
    std::vector<BYTE> buf(size);
    ImageCodecInfo* info = (ImageCodecInfo*)buf.data();
    if (GetImageEncoders(num, size, info) != Ok) return false;
    for (UINT i = 0; i < num; i++) {
        if (wcscmp(info[i].MimeType, mime) == 0) { *out = info[i].Clsid; return true; }
    }
    return false;
}

static int RunShot(const std::wstring& path, int W, int H, const std::wstring& state) {
    g.shotMode = true;
    g_dpi = 1.f;
    ComputeLayout((float)W, (float)H, 1.f);
    BuildFonts(1.f);
    BuildWheel(g.L.R);
    g.layerDirty = true;
    BuildLayer(W, H);
    if (state == L"spin") {
        g.rot = 141.0;
        g.angSpeed = 1100.0;
        g.spinning = true;
        g.wob = 9.0f;
        g.wobV = 0.f;
        g.lastResult = L"麻辣烫";
    } else if (state == L"result") {
        int wi = 3;
        if (wi >= (int)g.items.size()) wi = 0;
        double st = 0, sp = 0;
        SliceAngles(wi, &st, &sp);
        double r = fmod(-(st + sp * 0.5), 360.0);
        if (r < 0) r += 360.0;
        g.rot = r;
        g.winner = wi;
        g.resultT = 0.62;
        g.lastResult = g.items[wi].name;
        SpawnConfetti(170);
        for (int i = 0; i < 40; i++) AdvanceConfetti(0.015f);
        g.wob = -1.2f;
    }
    Bitmap bmp(W, H, PixelFormat32bppPARGB);
    {
        Graphics g2(&bmp);
        g2.SetSmoothingMode(SmoothingModeAntiAlias);
        g2.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        SolidBrush bg(RGBA(10, 12, 22));
        g2.FillRectangle(&bg, 0, 0, W, H);
        RenderFrame(g2, W, H);
    }
    CLSID clsid;
    if (!GetEncoderClsid(L"image/png", &clsid)) return 2;
    Status s = bmp.Save(path.c_str(), &clsid, nullptr);
    return (s == Ok) ? 0 : 3;
}

// ============================ 入口 ========================================

// 生成程序图标（一次性的开发用功能，--icon 输出 ico 文件）
static void DrawIconArt(Graphics& g2, int N) {
    float c = N * 0.5f, R = N * 0.40f;
    static const int cols[6][3] = {
        {247, 107, 107}, {255, 205, 60}, {81, 207, 102},
        { 34, 184, 207}, {124, 148, 252}, {247, 131, 172},
    };
    for (int i = 0; i < 6; i++) {
        GraphicsPath p;
        RectF box(c - R, c - R, R * 2, R * 2);
        p.AddPie(box, i * 60.f - 90.f, 60.f);
        SolidBrush b(RGBA(cols[i][0], cols[i][1], cols[i][2]));
        g2.FillPath(&b, &p);
    }
    GraphicsPath outer, inner;
    float ro = R * 1.20f;
    outer.AddEllipse(c - ro, c - ro, ro * 2, ro * 2);
    inner.AddEllipse(c - R, c - R, R * 2, R * 2);
    Region reg(&outer);
    reg.Exclude(&inner);
    LinearGradientBrush gold(PointF(c - ro, c - ro), PointF(c + ro, c + ro),
                             RGBA(255, 248, 216), RGBA(184, 130, 26));
    g2.FillRegion(&gold, &reg);
    SolidBrush hub(RGBA(30, 34, 56));
    g2.FillEllipse(&hub, c - R * 0.30f, c - R * 0.30f, R * 0.60f, R * 0.60f);
    Pen edge(RGBA(255, 226, 156), R * 0.09f);
    g2.DrawEllipse(&edge, c - R * 0.30f, c - R * 0.30f, R * 0.60f, R * 0.60f);
    SolidBrush tri(RGBA(255, 232, 160));
    PointF tp[3] = { PointF(c - R * 0.07f, c - R * 0.16f),
                     PointF(c - R * 0.07f, c + R * 0.16f),
                     PointF(c + R * 0.17f, c) };
    GraphicsPath tr;
    tr.AddPolygon(tp, 3);
    g2.FillPath(&tri, &tr);
}

static bool WriteIco(const std::wstring& path, const int* sizes, int nSizes) {
    std::vector<std::vector<BYTE>> blobs;
    for (int s = 0; s < nSizes; s++) {
        int N = sizes[s];
        int SS = 4;
        Bitmap big(N * SS, N * SS, PixelFormat32bppPARGB);
        {
            Graphics gb(&big);
            gb.SetSmoothingMode(SmoothingModeAntiAlias);
            gb.SetCompositingQuality(CompositingQualityHighQuality);
            gb.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            DrawIconArt(gb, N * SS);
        }
        Bitmap sml(N, N, PixelFormat32bppPARGB);
        {
            Graphics gs(&sml);
            gs.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            gs.SetPixelOffsetMode(PixelOffsetModeHighQuality);
            gs.DrawImage(&big, 0, 0, N, N);
        }
        Rect rc(0, 0, N, N);
        BitmapData bd;
        if (sml.LockBits(&rc, ImageLockModeRead, PixelFormat32bppARGB, &bd) != Ok) return false;
        DWORD maskRow = ((DWORD)((N + 31) / 32)) * 4;
        DWORD imgSize = 40 + (DWORD)(N * N * 4) + maskRow * N;
        std::vector<BYTE> blob(imgSize, 0);
        BYTE* p = blob.data();
        auto put16 = [&](BYTE* d, int v) { d[0] = (BYTE)(v & 0xFF); d[1] = (BYTE)((v >> 8) & 0xFF); };
        auto put32 = [&](BYTE* d, DWORD v) { for (int i = 0; i < 4; i++) d[i] = (BYTE)((v >> (8 * i)) & 0xFF); };
        put32(p + 0, 40);
        put32(p + 4, (DWORD)N);
        put32(p + 8, (DWORD)(N * 2));    // ICO 的位图高度含掩码
        put16(p + 12, 1);
        put16(p + 14, 32);
        BYTE* px = p + 40;
        for (int y = 0; y < N; y++) {
            const BYTE* src = (const BYTE*)bd.Scan0 + (size_t)(N - 1 - y) * bd.Stride;
            memcpy(px + (size_t)y * N * 4, src, (size_t)N * 4);
        }
        sml.UnlockBits(&bd);
        blobs.push_back(std::move(blob));
    }
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) return false;
    auto w16 = [&](int v) { BYTE b[2] = { (BYTE)(v & 0xFF), (BYTE)((v >> 8) & 0xFF) }; fwrite(b, 1, 2, f); };
    auto w32 = [&](DWORD v) { BYTE b[4]; for (int i = 0; i < 4; i++) b[i] = (BYTE)((v >> (8 * i)) & 0xFF); fwrite(b, 1, 4, f); };
    w16(0); w16(1); w16(nSizes);
    DWORD off = 6 + 16 * (DWORD)nSizes;
    for (int s = 0; s < nSizes; s++) {
        BYTE wh = (BYTE)(sizes[s] >= 256 ? 0 : sizes[s]);
        BYTE b[16];
        b[0] = wh; b[1] = wh; b[2] = 0; b[3] = 0;
        b[4] = 1; b[5] = 0; b[6] = 32; b[7] = 0;
        DWORD len = (DWORD)blobs[s].size();
        for (int i = 0; i < 4; i++) b[8 + i] = (BYTE)((len >> (8 * i)) & 0xFF);
        for (int i = 0; i < 4; i++) b[12 + i] = (BYTE)((off >> (8 * i)) & 0xFF);
        fwrite(b, 1, 16, f);
        off += len;
    }
    for (auto& bl : blobs) fwrite(bl.data(), 1, bl.size(), f);
    fclose(f);
    return true;
}

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring shotPath, shotState = L"idle";
    std::wstring iconPath;
    int shotW = 1280, shotH = 820;
    bool wantShot = false;
    for (int i = 1; i < argc; i++) {
        std::wstring a = argv[i];
        if (a == L"--shot" && i + 1 < argc) { shotPath = argv[++i]; wantShot = true; }
        else if (a == L"--icon" && i + 1 < argc) { iconPath = argv[++i]; }
        else if (a == L"--state" && i + 1 < argc) { shotState = argv[++i]; }
        else if (a == L"--size" && i + 1 < argc) {
            std::wstring s = argv[++i];
            size_t x = s.find_first_of(L"xX");
            if (x != std::wstring::npos) { shotW = _wtoi(s.substr(0, x).c_str()); shotH = _wtoi(s.substr(x + 1).c_str()); }
        }
    }
    if (argv) LocalFree(argv);

    GdiplusStartupInput gsi;
    ULONG_PTR token = 0;
    GdiplusStartup(&token, &gsi, nullptr);
    g.rng.seed(((unsigned long long)std::random_device{}() << 21) ^ (unsigned long long)(NowSec() * 1e6));
    BuildSounds();
    if (!LoadConfig()) {
        LoadPreset(0);
        SaveConfig();     // 首次运行就生成配置文件，方便用户直接用记事本改
    }

    if (!iconPath.empty()) {
        int sizes[4] = { 16, 32, 48, 256 };
        bool ok = WriteIco(iconPath, sizes, 4);
        GdiplusShutdown(token);
        return ok ? 0 : 4;
    }
    if (wantShot) {
        int rc = RunShot(shotPath, shotW, shotH, shotState);
        GdiplusShutdown(token);
        return rc;
    }

    HMODULE u32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL(WINAPI * SetDpiCtxFn)(HANDLE);
    if (auto fn = (SetDpiCtxFn)GetProcAddress(u32, "SetProcessDpiAwarenessContext")) fn((HANDLE)-4);

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"XiaoZhuanPanWnd";
    RegisterClassExW(&wc);

    typedef UINT(WINAPI * GetDpiFn)();
    UINT dpi = 96;
    if (auto fn = (GetDpiFn)GetProcAddress(u32, "GetDpiForSystem")) dpi = fn();
    g_dpi = dpi / 96.f;

    RECT wa = { 0, 0, 1920, 1080 };
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int scrW = wa.right - wa.left, scrH = wa.bottom - wa.top;
    int cw = (int)(1280 * g_dpi), ch = (int)(880 * g_dpi);
    if (cw > scrW - 60) cw = scrW - 60;
    if (ch > scrH - 80) ch = scrH - 80;
    RECT r = { 0, 0, cw, ch };
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&r, style, FALSE);
    int ow = r.right - r.left, oh = r.bottom - r.top;
    int ox = wa.left + (scrW - ow) / 2;
    int oy = wa.top + (scrH - oh) / 2;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, kTitle, style, ox, oy, ow, oh,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) { GdiplusShutdown(token); return 1; }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (g.layer) { delete g.layer; g.layer = nullptr; }
    if (g.wheel) { delete g.wheel; g.wheel = nullptr; }
    if (g.hFontUI) DeleteObject(g.hFontUI);
    if (g.editBrush) DeleteObject(g.editBrush);
    GdiplusShutdown(token);
    return 0;
}
