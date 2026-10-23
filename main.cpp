#define _CRT_SECURE_NO_WARNINGS
#define _SILENCE_CXX20_U8PATH_DEPRECATION_WARNING
#ifndef NOMINMAX
#define NOMINMAX
#endif
#pragma execution_character_set("utf-8")
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <tchar.h>
#include <dwmapi.h>
#include <string>
#include <vector>
#include <shellapi.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <shobjidl.h>
#include "resource.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include "qypxr_core.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "winmm.lib")

// ============================================================================
//  D3D
// ============================================================================
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static HWND                    g_hwnd = nullptr;

bool CreateDeviceD3D(HWND hWnd); void CleanupDeviceD3D(); void CreateRenderTarget(); void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ============================================================================
//  Состояние приложения
// ============================================================================
std::string g_SelectedFilePath = "";
std::string g_DroppedFilePath = "";
int  g_TargetFps = 400;
std::atomic<bool> g_IsProcessing(false);
std::mutex g_UIMutex;

struct DoneItem { std::string path; size_t orig; size_t proc; double sec; };
static std::vector<DoneItem> g_Done;

struct ToastNotification { std::string text; float timer; };
static std::vector<ToastNotification> g_Toasts;

static bool  g_AnimBg = true, g_ParticlesOn = true, g_ToastsOn = true, g_AutoOpen = false, g_RememberFps = true;
static int   g_AccentIdx = 0;
static bool  g_IsLightTheme = false;
static float g_ThemeAnim = 0.0f;
static int   g_Page = 0;
static float g_PageAlpha = 0.0f;
static float g_NavHover[3] = { 0, 0, 0 };
static float g_DropHover = 0.0f;
static float g_A = 1.0f;                       // глобальная прозрачность контента (анимация смены страниц)

struct Particle { float angle, radius, speed, size, alpha, base_radius; };
static std::vector<Particle> g_Particles;
struct BgStar { float x, y, size, phase, speed; };   // x,y нормализованы 0..1
static std::vector<BgStar> g_Stars;

static ImFont* g_FontBody = nullptr;
static ImFont* g_FontSmall = nullptr;
static ImFont* g_FontH2 = nullptr;
static ImFont* g_FontH1 = nullptr;
static ImFont* g_FontBig = nullptr;   // светлый крупный (цифры)
static ImFont* g_FontMono = nullptr;  // моно-подписи

static const std::vector<std::pair<std::string, std::string>> g_Quotes = {
    { "Воображение важнее знания. Знания ограничены, тогда как воображение охватывает целый мир.", "Альберт Эйнштейн" },
    { "Ничто в жизни не следует бояться, это нужно только понять.", "Мария Склодовская-Кюри" },
    { "Если я видел дальше других, то потому, что стоял на плечах гигантов.", "Исаак Ньютон" },
    { "Настоящее принадлежит им; будущее, ради которого я работал, принадлежит мне.", "Никола Тесла" },
    { "Интеллект — это способность адаптироваться к изменениям.", "Стивен Хокинг" }
};
static int g_CurrentQuoteIdx = 0;

// ============================================================================
//  Утилиты
// ============================================================================
static float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static ImVec4 LerpColor(const ImVec4& a, const ImVec4& b, float t) { return ImVec4(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)); }
static ImVec4 Rgb(int r, int g, int b, float a = 1.0f) { return ImVec4(r / 255.f, g / 255.f, b / 255.f, a); }
static ImU32 Col(const ImVec4& c, float a = 1.0f) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.x, c.y, c.z, c.w * a * g_A)); }
static float Smooth(float t) { return t * t * (3.0f - 2.0f * t); }

// Палитра «Горизонт событий»: почти-чёрный с тёплым оттенком, янтарный акцент.
// Поверхности полупрозрачные (панели не «коробки», а лёгкие стёкла поверх атмосферы).
struct Palette { ImVec4 bg, panel, panel2, solid, border, text, dim; };
static Palette g_P;
static ImVec4 ACCENT = Rgb(255, 138, 56);
static ImVec4 ACCENT_HI = Rgb(255, 186, 120);
static const ImVec4 CYAN = Rgb(120, 200, 255);
static const ImVec4 WHITE = ImVec4(1, 1, 1, 1);
static const ImVec4 INK = Rgb(16, 13, 10);

static void UpdatePalette() {
    Palette dk = { Rgb(7,8,11), ImVec4(1.f,0.96f,0.90f,0.030f), ImVec4(1.f,0.96f,0.90f,0.075f), Rgb(22,21,23), ImVec4(1.f,0.95f,0.88f,0.100f), Rgb(233,228,218), Rgb(136,131,124) };
    Palette lt = { Rgb(236,231,221), ImVec4(1.f,1.f,1.f,0.50f), ImVec4(0.10f,0.08f,0.05f,0.060f), Rgb(250,247,241), ImVec4(0.10f,0.08f,0.05f,0.150f), Rgb(26,24,22), Rgb(112,106,98) };
    g_P.bg = LerpColor(dk.bg, lt.bg, g_ThemeAnim);
    g_P.panel = LerpColor(dk.panel, lt.panel, g_ThemeAnim);
    g_P.panel2 = LerpColor(dk.panel2, lt.panel2, g_ThemeAnim);
    g_P.solid = LerpColor(dk.solid, lt.solid, g_ThemeAnim);
    g_P.border = LerpColor(dk.border, lt.border, g_ThemeAnim);
    g_P.text = LerpColor(dk.text, lt.text, g_ThemeAnim);
    g_P.dim = LerpColor(dk.dim, lt.dim, g_ThemeAnim);
}
// Акцент для текста/линий: на светлой теме затемняется, чтобы читался
static ImVec4 AccentT() { return LerpColor(ACCENT_HI, ImVec4(ACCENT.x * 0.68f, ACCENT.y * 0.68f, ACCENT.z * 0.68f, 1.f), g_ThemeAnim); }

static std::string WStringToString(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string s(n, 0); WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &s[0], n, NULL, NULL); return s;
}
static std::wstring StringToWString(const std::string& str) {
    if (str.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring w(n, 0); MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &w[0], n); return w;
}
static std::string OpenVideoDialog(HWND hwnd) {
    OPENFILENAMEW ofn; wchar_t szFile[1024] = { 0 }; ZeroMemory(&ofn, sizeof(ofn)); ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd; ofn.lpstrFile = szFile; ofn.nMaxFile = 1024;
    ofn.lpstrFilter = L"Видео файлы\0*.mp4;*.mov\0Все файлы\0*.*\0"; ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn) == TRUE) return WStringToString(szFile);
    return "";
}
static void AddToast(const std::string& message) { std::lock_guard<std::mutex> lock(g_UIMutex); g_Toasts.push_back({ message, 3.0f }); }

static std::string FileNameOf(const std::string& p) { size_t i = p.find_last_of("\\/"); return i == std::string::npos ? p : p.substr(i + 1); }
static std::string DirOf(const std::string& p) { size_t i = p.find_last_of("\\/"); return i == std::string::npos ? std::string() : p.substr(0, i); }
static std::string FmtSize(uint64_t b) {
    char buf[48];
    if (b < 1024) snprintf(buf, sizeof(buf), "%llu Б", (unsigned long long)b);
    else if (b < 1024ull * 1024) snprintf(buf, sizeof(buf), "%.1f КБ", b / 1024.0);
    else if (b < 1024ull * 1024 * 1024) snprintf(buf, sizeof(buf), "%.2f МБ", b / (1024.0 * 1024.0));
    else snprintf(buf, sizeof(buf), "%.2f ГБ", b / (1024.0 * 1024.0 * 1024.0));
    return buf;
}
static uint64_t FileSizeOf(const std::string& p) {
    std::error_code ec; auto s = std::filesystem::file_size(std::filesystem::u8path(p), ec); return ec ? 0 : (uint64_t)s;
}
static void OpenUrl(const wchar_t* url) { ShellExecuteW(NULL, L"open", url, NULL, NULL, SW_SHOWNORMAL); }

static void InitStars() {
    if (!g_Stars.empty()) return;
    // размер = «глубина»: мелких много, крупных единицы; крупные дрейфуют быстрее (параллакс)
    for (int i = 0; i < 130; ++i)
        g_Stars.push_back({ (rand() % 1000) / 1000.f, (rand() % 1000) / 1000.f, powf((rand() % 1000) / 1000.f, 2.4f) * 1.4f + 0.45f, (rand() % 628) / 100.f, (rand() % 20) / 10.f + 0.6f });
}
static void InitParticles() {
    if (!g_Particles.empty()) return;
    for (int i = 0; i < 110; ++i) {
        Particle p;
        p.angle = (rand() % 360) * 3.14159f / 180.f;
        p.base_radius = (float)(rand() % 110 + 60); p.radius = p.base_radius * ((rand() % 100) / 100.f);
        p.speed = (float)(rand() % 50 + 20) * 0.001f; p.size = (float)(rand() % 3) + 1.8f; p.alpha = (float)(rand() % 255);
        g_Particles.push_back(p);
    }
}

// ============================================================================
//  Звук: всё синтезируется при запуске (никаких файлов), играется через winmm
// ============================================================================
enum SndId { SFX_HOVER, SFX_CLICK, SFX_ON, SFX_OFF, SFX_PICK, SFX_RUN, SFX_OK, SFX_ERR, SFX_START, SFX_COUNT };
static bool g_SoundOn = true;
static std::vector<uint8_t> g_Wav[SFX_COUNT];
static std::atomic<int> g_PendingSnd(-1);        // звук из рабочего потока → проигрывается в главном цикле
static double g_SndMuteUntil = 0.0, g_LastHoverSnd = 0.0;

static const int SR = 44100;
static void SynthTone(std::vector<float>& o, float t0, float dur, float f0, float f1, float amp, float attack, float decay) {
    size_t i0 = (size_t)(t0 * SR), n = (size_t)(dur * SR); if (o.size() < i0 + n) o.resize(i0 + n, 0.f);
    double ph = 0.0;
    for (size_t i = 0; i < n; ++i) {
        float t = (float)i / SR, k = (float)i / (float)n;
        float f = f0 + (f1 - f0) * k; ph += 6.2831853 * f / SR;
        float env = std::min(1.f, t / std::max(attack, 0.0005f)) * expf(-t * decay);
        o[i0 + i] += amp * env * (sinf((float)ph) + 0.28f * sinf((float)ph * 2.f) + 0.08f * sinf((float)ph * 3.f));
    }
}
static void SynthNoise(std::vector<float>& o, float t0, float dur, float amp, float lp, float attack, float decay) {
    size_t i0 = (size_t)(t0 * SR), n = (size_t)(dur * SR); if (o.size() < i0 + n) o.resize(i0 + n, 0.f);
    float y = 0.f; unsigned seed = 12345u + (unsigned)(t0 * 1000);
    for (size_t i = 0; i < n; ++i) {
        seed = seed * 1664525u + 1013904223u; float r = ((seed >> 9) / 4194304.f) - 1.f;
        y += (r - y) * lp; float t = (float)i / SR;
        o[i0 + i] += amp * y * std::min(1.f, t / std::max(attack, 0.0005f)) * expf(-t * decay);
    }
}
static void SynthEcho(std::vector<float>& o, float delay, float gain, int taps) {
    size_t d = (size_t)(delay * SR); o.resize(o.size() + d * taps, 0.f);
    for (size_t i = d; i < o.size(); ++i) o[i] += o[i - d] * gain;
}
static std::vector<uint8_t> ToWav(std::vector<float>& s, float vol) {
    std::vector<uint8_t> w(44 + s.size() * 2);
    auto p32 = [&](size_t o, uint32_t v) { memcpy(&w[o], &v, 4); }; auto p16 = [&](size_t o, uint16_t v) { memcpy(&w[o], &v, 2); };
    memcpy(&w[0], "RIFF", 4); p32(4, (uint32_t)(36 + s.size() * 2)); memcpy(&w[8], "WAVEfmt ", 8);
    p32(16, 16); p16(20, 1); p16(22, 1); p32(24, SR); p32(28, SR * 2); p16(32, 2); p16(34, 16); memcpy(&w[36], "data", 4); p32(40, (uint32_t)(s.size() * 2));
    for (size_t i = 0; i < s.size(); ++i) {
        float x = tanhf(s[i] * vol * 1.2f);                              // мягкий лимитер
        float fade = std::min(1.f, (s.size() - i) / (0.01f * SR));       // без щелчка в конце
        p16(44 + i * 2, (uint16_t)(int16_t)(x * fade * 30000.f));
    }
    return w;
}
static void BuildSounds() {
    std::vector<float> s;
    s.clear(); SynthTone(s, 0, 0.035f, 3300, 3100, 0.07f, 0.002f, 90);                                                   g_Wav[SFX_HOVER] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.09f, 1500, 800, 0.30f, 0.001f, 48); SynthNoise(s, 0, 0.02f, 0.10f, 0.5f, 0.001f, 160);   g_Wav[SFX_CLICK] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.14f, 660, 660, 0.24f, 0.002f, 22); SynthTone(s, 0.06f, 0.18f, 990, 990, 0.24f, 0.002f, 20); g_Wav[SFX_ON] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.14f, 990, 990, 0.22f, 0.002f, 24); SynthTone(s, 0.06f, 0.18f, 560, 560, 0.22f, 0.002f, 20); g_Wav[SFX_OFF] = ToWav(s, 0.9f);
    s.clear(); SynthNoise(s, 0, 0.26f, 0.22f, 0.18f, 0.08f, 11); SynthTone(s, 0, 0.28f, 420, 900, 0.20f, 0.02f, 11);       g_Wav[SFX_PICK] = ToWav(s, 0.9f);
    s.clear(); SynthTone(s, 0, 0.42f, 110, 340, 0.34f, 0.04f, 6); SynthTone(s, 0, 0.42f, 220, 680, 0.14f, 0.04f, 6); SynthNoise(s, 0, 0.30f, 0.08f, 0.10f, 0.12f, 7); g_Wav[SFX_RUN] = ToWav(s, 0.9f);
    {   // успех: арпеджио C-E-G-C с «залом»
        s.clear(); const float nt[4] = { 523.25f, 659.25f, 783.99f, 1046.5f };
        for (int i = 0; i < 4; ++i) SynthTone(s, i * 0.09f, 1.0f, nt[i], nt[i], 0.22f, 0.004f, 4.2f);
        SynthEcho(s, 0.17f, 0.36f, 3); g_Wav[SFX_OK] = ToWav(s, 0.85f);
    }
    s.clear(); SynthTone(s, 0, 0.30f, 196, 140, 0.34f, 0.004f, 8); SynthTone(s, 0, 0.30f, 207, 148, 0.24f, 0.004f, 8); SynthTone(s, 0.17f, 0.30f, 165, 110, 0.28f, 0.004f, 8); g_Wav[SFX_ERR] = ToWav(s, 0.9f);
    {   // запуск: нарастающий гул + мерцание
        s.clear(); SynthTone(s, 0, 1.2f, 80, 330, 0.26f, 0.45f, 2.0f); SynthTone(s, 0, 1.2f, 160, 660, 0.10f, 0.50f, 2.2f);
        SynthNoise(s, 0, 1.1f, 0.06f, 0.05f, 0.5f, 2.2f);
        SynthTone(s, 0.45f, 0.9f, 1318.5f, 1318.5f, 0.07f, 0.04f, 3.5f); SynthTone(s, 0.62f, 0.9f, 1760.f, 1760.f, 0.05f, 0.04f, 3.5f);
        SynthEcho(s, 0.23f, 0.42f, 3); g_Wav[SFX_START] = ToWav(s, 0.8f);
    }
}
static void Snd(int id) {
    if (!g_SoundOn || id < 0 || id >= SFX_COUNT || g_Wav[id].empty()) return;
    double now = ImGui::GetTime();
    if (id == SFX_HOVER) { if (now < g_SndMuteUntil || now - g_LastHoverSnd < 0.07) return; g_LastHoverSnd = now; }
    else if (id == SFX_OK || id == SFX_ERR) g_SndMuteUntil = now + 1.3;
    else if (now < g_SndMuteUntil) return;
    PlaySoundW((LPCWSTR)g_Wav[id].data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}
// звук наведения (по фронту) + клика для только что созданного элемента
static bool Fx(bool pressed) {
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID id = ImGui::GetItemID();
    bool hov = ImGui::IsItemHovered(), prev = st->GetBool(id ^ 0x5F3759DF, false);
    if (hov && !prev) Snd(SFX_HOVER);
    st->SetBool(id ^ 0x5F3759DF, hov);
    if (pressed) Snd(SFX_CLICK);
    return pressed;
}
// ============================================================================
//  Примитивы рисования
// ============================================================================
static ImVec2 TSize(ImFont* f, const char* s) { ImGui::PushFont(f); ImVec2 v = ImGui::CalcTextSize(s); ImGui::PopFont(); return v; }
static void DText(ImDrawList* d, ImFont* f, ImVec2 p, ImU32 c, const char* s) { ImGui::PushFont(f); d->AddText(p, c, s); ImGui::PopFont(); }
static void DTextC(ImDrawList* d, ImFont* f, float cx, float y, ImU32 c, const char* s) { ImVec2 sz = TSize(f, s); DText(d, f, ImVec2(cx - sz.x * 0.5f, y), c, s); }
static void DTextR(ImDrawList* d, ImFont* f, float rx, float y, ImU32 c, const char* s) { ImVec2 sz = TSize(f, s); DText(d, f, ImVec2(rx - sz.x, y), c, s); }

static void Poly3(ImDrawList* d, ImVec2 a, ImVec2 b, ImVec2 c, ImU32 col, float th) { ImVec2 pts[3] = { a, b, c }; d->AddPolyline(pts, 3, col, 0, th); }

static int U8Len(unsigned char c) { return c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4; }

// Текст с разрядкой (tracking) — основа «инструментального» типографского стиля
static float TrackWidth(ImFont* f, const char* s, float track) {
    ImGui::PushFont(f); float w = 0.f;
    for (const char* p = s; *p; ) { int n = U8Len((unsigned char)*p); w += ImGui::CalcTextSize(p, p + n).x + track; p += n; }
    ImGui::PopFont(); return w > 0.f ? w - track : 0.f;
}
static float DTrack(ImDrawList* d, ImFont* f, ImVec2 p, ImU32 col, const char* s, float track) {
    ImGui::PushFont(f); float x = p.x;
    for (const char* q = s; *q; ) { int n = U8Len((unsigned char)*q); d->AddText(ImVec2(x, p.y), col, q, q + n); x += ImGui::CalcTextSize(q, q + n).x + track; q += n; }
    ImGui::PopFont(); return (x - p.x) - track;
}
static float DLabel(ImDrawList* d, ImVec2 p, ImU32 col, const char* s, float track = 1.8f) { return DTrack(d, g_FontMono, p, col, s, track); }
static void DLabelC(ImDrawList* d, float cx, float y, ImU32 col, const char* s, float track = 1.8f) { DLabel(d, ImVec2(cx - TrackWidth(g_FontMono, s, track) * 0.5f, y), col, s, track); }
static void DLabelR(ImDrawList* d, float rx, float y, ImU32 col, const char* s, float track = 1.8f) { DLabel(d, ImVec2(rx - TrackWidth(g_FontMono, s, track), y), col, s, track); }

// «01 — ЗАГОЛОВОК»: индекс акцентом, подпись приглушённо
static void Eyebrow(ImDrawList* d, ImVec2 p, const char* idx, const char* text) {
    float w = DLabel(d, p, Col(AccentT()), idx, 1.5f);
    d->AddLine(ImVec2(p.x + w + 8, p.y + 8), ImVec2(p.x + w + 20, p.y + 8), Col(g_P.dim, 0.5f), 1.f);
    DLabel(d, ImVec2(p.x + w + 28, p.y), Col(g_P.dim), text);
}

static float DrawWrapped(ImDrawList* d, ImFont* f, ImVec2 p, float maxW, ImU32 c, const std::string& text, float lineH) {
    ImGui::PushFont(f);
    std::vector<std::string> words; { std::string w; for (char ch : text) { if (ch == ' ') { words.push_back(w); w.clear(); } else w += ch; } words.push_back(w); }
    std::string cur; float y = p.y;
    for (const auto& w : words) {
        std::string t = cur.empty() ? w : cur + " " + w;
        if (!cur.empty() && ImGui::CalcTextSize(t.c_str()).x > maxW) { d->AddText(ImVec2(p.x, y), c, cur.c_str()); y += lineH; cur = w; }
        else cur = t;
    }
    if (!cur.empty()) { d->AddText(ImVec2(p.x, y), c, cur.c_str()); y += lineH; }
    ImGui::PopFont();
    return y - p.y;
}

// Угловые скобки «видоискателя»
static void Brackets(ImDrawList* d, ImVec2 a, ImVec2 b, float L, ImU32 col, float th) {
    d->AddLine(a, ImVec2(a.x + L, a.y), col, th);                 d->AddLine(a, ImVec2(a.x, a.y + L), col, th);
    d->AddLine(ImVec2(b.x, a.y), ImVec2(b.x - L, a.y), col, th);  d->AddLine(ImVec2(b.x, a.y), ImVec2(b.x, a.y + L), col, th);
    d->AddLine(ImVec2(a.x, b.y), ImVec2(a.x + L, b.y), col, th);  d->AddLine(ImVec2(a.x, b.y), ImVec2(a.x, b.y - L), col, th);
    d->AddLine(b, ImVec2(b.x - L, b.y), col, th);                 d->AddLine(b, ImVec2(b.x, b.y - L), col, th);
}

// Панель: лёгкое стекло, тонкая рамка, блик по верхней кромке, акцентные скобки по углам
static void Panel(ImDrawList* d, ImVec2 a, ImVec2 b, bool brackets = true) {
    const float r = 4.f; float dk = 1.f - g_ThemeAnim;
    d->AddRectFilled(a, b, Col(g_P.panel), r);
    d->AddRectFilledMultiColor(ImVec2(a.x + r, a.y), ImVec2(b.x - r, a.y + 1.f), Col(WHITE, 0.20f * dk), Col(WHITE, 0.f), Col(WHITE, 0.f), Col(WHITE, 0.20f * dk));
    d->AddRect(a, b, Col(g_P.border), r, 0, 1.f);
    if (brackets) Brackets(d, ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), 9.f, Col(AccentT(), 0.65f), 1.2f);
}

static void Glow(ImDrawList* d, ImVec2 c, float radius, const ImVec4& color, float totalAlpha, int steps = 40) {
    for (int i = 0; i < steps; ++i) { float t = (float)i / steps; d->AddCircleFilled(c, radius * (1.f - t), Col(color, totalAlpha / steps * (0.6f + t)), 64); }
}

static float Hash01(int n) {
    unsigned x = (unsigned)n * 747796405u + 2891336453u; x = ((x >> ((x >> 28) + 4)) ^ x) * 277803737u; x = (x >> 22) ^ x; return x / 4294967295.f;
}

// state: 0 = выключена, 1 = активна, 2 = занята
static bool PrimaryButton(const char* id, const char* label, ImVec2 pos, ImVec2 size, int state, float time, float prog = 0.f) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = ImGui::InvisibleButton(id, size);
    if (state == 1) Fx(pressed);
    bool hov = (state == 1) && ImGui::IsItemHovered();
    ImVec2 b(pos.x + size.x, pos.y + size.y); const float r = 4.f;
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
    float h = st->GetFloat(gid, 0.f); h = Lerp(h, hov ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 12.f)); st->SetFloat(gid, h);
    if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImU32 txt = Col(INK);
    if (state == 1) {
        for (int i = 1; i <= 12; ++i) d->AddRect(ImVec2(pos.x - i, pos.y - i), ImVec2(b.x + i, b.y + i), Col(ACCENT, 0.05f * h * (1.f - i / 13.f)), r + i);
        d->AddRectFilled(pos, b, Col(LerpColor(ACCENT, ACCENT_HI, h)), r);
        d->AddRectFilledMultiColor(ImVec2(pos.x + 1, pos.y + 1), ImVec2(b.x - 1, pos.y + size.y * 0.55f), Col(WHITE, 0.22f), Col(WHITE, 0.22f), Col(WHITE, 0.f), Col(WHITE, 0.f));
        // световая полоса пробегает при наведении
        d->PushClipRect(pos, b, true);
        float sx = pos.x - 60.f + h * (size.x + 120.f);
        d->AddQuadFilled(ImVec2(sx, pos.y), ImVec2(sx + 26, pos.y), ImVec2(sx + 6, b.y), ImVec2(sx - 20, b.y), Col(WHITE, 0.28f * sinf(h * 3.14159f)));
        d->PopClipRect();
    }
    else if (state == 2) {
        float fw = size.x * std::clamp(prog, 0.f, 1.f);
        d->AddRectFilled(pos, b, Col(ACCENT, 0.20f), r);
        d->AddRect(pos, b, Col(ACCENT, 0.55f), r);
        d->PushClipRect(pos, ImVec2(pos.x + fw, b.y), true);
        d->AddRectFilled(pos, b, Col(LerpColor(ACCENT, ACCENT_HI, 0.2f)), r);
        float off = fmodf(time * 34.f, 18.f);
        for (float x = pos.x - size.y - 18.f + off; x < b.x + 18.f; x += 18.f) d->AddLine(ImVec2(x, b.y), ImVec2(x + size.y, pos.y), Col(INK, 0.14f), 7.f);
        d->PopClipRect();
        if (fw > 4.f) { for (int i = 1; i <= 6; ++i) d->AddRectFilled(ImVec2(pos.x + fw - 1.f, pos.y), ImVec2(pos.x + fw + i * 2.f, b.y), Col(ACCENT_HI, 0.05f), 0.f); d->AddRectFilled(ImVec2(pos.x + fw - 2.f, pos.y), ImVec2(pos.x + fw, b.y), Col(WHITE, 0.75f)); }
        txt = Col(INK);
    }
    else {
        d->AddRectFilled(pos, b, Col(g_P.panel), r);
        d->AddRect(pos, b, Col(g_P.border), r);
        txt = Col(g_P.dim);
    }
    float tw = TrackWidth(g_FontH2, label, 2.2f); ImVec2 ts = TSize(g_FontH2, label);
    float tx = pos.x + (size.x - tw) * 0.5f - (state == 1 ? 10.f : 0.f);
    if (state == 2) {   // слева от кромки прогресса текст тёмный, справа — светлый
        float fw = size.x * std::clamp(prog, 0.f, 1.f);
        d->PushClipRect(pos, ImVec2(pos.x + fw, b.y), true); DTrack(d, g_FontH2, ImVec2(tx, pos.y + (size.y - ts.y) * 0.5f), Col(INK), label, 2.2f); d->PopClipRect();
        d->PushClipRect(ImVec2(pos.x + fw, pos.y), b, true); DTrack(d, g_FontH2, ImVec2(tx, pos.y + (size.y - ts.y) * 0.5f), Col(g_P.text), label, 2.2f); d->PopClipRect();
    }
    else
        DTrack(d, g_FontH2, ImVec2(tx, pos.y + (size.y - ts.y) * 0.5f), txt, label, 2.2f);
    if (state == 1) {   // стрелка
        float ax = tx + tw + 18.f + h * 5.f, ay = pos.y + size.y * 0.5f;
        d->AddLine(ImVec2(ax, ay), ImVec2(ax + 16, ay), txt, 1.8f);
        d->AddLine(ImVec2(ax + 10, ay - 5), ImVec2(ax + 16, ay), txt, 1.8f); d->AddLine(ImVec2(ax + 10, ay + 5), ImVec2(ax + 16, ay), txt, 1.8f);
    }
    return pressed && state == 1;
}

// Тонкая «призрачная» кнопка с моно-подписью
static bool GhostBtn(const char* id, const char* label, ImVec2 pos, ImVec2 size, bool active = false) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = Fx(ImGui::InvisibleButton(id, size));
    bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 b(pos.x + size.x, pos.y + size.y);
    d->AddRectFilled(pos, b, Col(hov ? g_P.panel2 : g_P.panel), 4.f);
    d->AddRect(pos, b, (hov || active) ? Col(AccentT(), 0.8f) : Col(g_P.border), 4.f, 0, 1.f);
    float tw = TrackWidth(g_FontMono, label, 1.4f); ImVec2 ts = TSize(g_FontMono, label);
    DLabel(d, ImVec2(pos.x + (size.x - tw) * 0.5f, pos.y + (size.y - ts.y) * 0.5f), (hov || active) ? Col(AccentT()) : Col(g_P.text), label, 1.4f);
    return pressed;
}

// Текстовая вкладка с подчёркиванием
static bool TabText(const char* id, const char* label, ImVec2 pos, float w, bool active) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = Fx(ImGui::InvisibleButton(id, ImVec2(w, 26)));
    bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    float tw = TrackWidth(g_FontMono, label, 1.2f); ImVec2 ts = TSize(g_FontMono, label);
    float tx = pos.x + (w - tw) * 0.5f;
    DLabel(d, ImVec2(tx, pos.y + (26 - ts.y) * 0.5f - 1.f), active ? Col(AccentT()) : (hov ? Col(g_P.text) : Col(g_P.dim)), label, 1.2f);
    if (active) d->AddRectFilled(ImVec2(tx, pos.y + 24.f), ImVec2(tx + tw, pos.y + 26.f), Col(ACCENT), 1.f);
    return pressed;
}

static bool LinkBtn(const char* id, const char* label, ImVec2 pos, ImVec2 size, const ImVec4& color) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pressed = Fx(ImGui::InvisibleButton(id, size));
    bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImVec2 b(pos.x + size.x, pos.y + size.y);
    d->AddRectFilled(pos, b, Col(hov ? g_P.panel2 : g_P.panel), 4.f);
    d->AddRect(pos, b, Col(g_P.border), 4.f);
    d->AddRectFilled(pos, ImVec2(pos.x + 3.f, b.y), Col(color), 2.f);
    ImVec2 ts = TSize(g_FontBody, label);
    DText(d, g_FontBody, ImVec2(pos.x + 18, pos.y + (size.y - ts.y) * 0.5f), Col(g_P.text), label);
    float ax = b.x - 28.f, ay = pos.y + size.y * 0.5f + (hov ? -1.f : 0.f), axx = ax + (hov ? 2.f : 0.f);
    ImU32 ac = Col(color, hov ? 1.f : 0.7f);
    d->AddLine(ImVec2(axx - 5, ay + 5), ImVec2(axx + 5, ay - 5), ac, 1.6f);
    d->AddLine(ImVec2(axx - 1, ay - 5), ImVec2(axx + 5, ay - 5), ac, 1.6f); d->AddLine(ImVec2(axx + 5, ay - 5), ImVec2(axx + 5, ay + 1), ac, 1.6f);
    return pressed;
}

struct AccentPreset { ImVec4 a, hi; const char* name; };
static const AccentPreset g_Accents[5] = {
    { Rgb(255, 138, 56),  Rgb(255, 186, 120), "Янтарь" },
    { Rgb(86, 168, 255),  Rgb(150, 205, 255), "Лёд" },
    { Rgb(64, 208, 168),  Rgb(130, 236, 204), "Мята" },
    { Rgb(240, 98, 132),  Rgb(255, 152, 174), "Роза" },
    { Rgb(160, 140, 255), Rgb(198, 184, 255), "Сумрак" }
};

static std::wstring SettingsPath() {
    wchar_t buf[MAX_PATH]; GetModuleFileNameW(NULL, buf, MAX_PATH); std::wstring p(buf);
    size_t i = p.find_last_of(L"\\/"); if (i != std::wstring::npos) p.resize(i + 1);
    return p + L"qypxr_settings.ini";
}
static void SaveSettings() {
    std::ofstream f(SettingsPath()); if (!f) return;
    f << "light=" << (g_IsLightTheme ? 1 : 0) << "\naccent=" << g_AccentIdx << "\nanimbg=" << (g_AnimBg ? 1 : 0)
        << "\nparticles=" << (g_ParticlesOn ? 1 : 0) << "\ntoasts=" << (g_ToastsOn ? 1 : 0) << "\nautoopen=" << (g_AutoOpen ? 1 : 0)
        << "\nsound=" << (g_SoundOn ? 1 : 0) << "\nremember=" << (g_RememberFps ? 1 : 0) << "\nfps=" << (g_RememberFps ? g_TargetFps : 400) << "\n";
}
static void LoadSettings() {
    std::ifstream f(SettingsPath()); std::string line; int fps = 400;
    while (std::getline(f, line)) {
        size_t e = line.find('='); if (e == std::string::npos) continue;
        std::string k = line.substr(0, e); int v = atoi(line.c_str() + e + 1);
        if (k == "light") { g_IsLightTheme = v != 0; g_ThemeAnim = v ? 1.f : 0.f; }
        else if (k == "accent") g_AccentIdx = std::clamp(v, 0, 4);
        else if (k == "animbg") g_AnimBg = v != 0;
        else if (k == "particles") g_ParticlesOn = v != 0;
        else if (k == "toasts") g_ToastsOn = v != 0;
        else if (k == "autoopen") g_AutoOpen = v != 0;
        else if (k == "remember") g_RememberFps = v != 0;
        else if (k == "sound") g_SoundOn = v != 0;
        else if (k == "fps") fps = v;
    }
    if (g_RememberFps) g_TargetFps = std::clamp(fps, 60, 1000);
    ACCENT = g_Accents[g_AccentIdx].a; ACCENT_HI = g_Accents[g_AccentIdx].hi;
}

// Переключатель: контур → заливка акцентом, тёмная «ручка»
static bool Switch(const char* id, ImVec2 pos, bool* v) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    bool pr = ImGui::InvisibleButton(id, ImVec2(52, 28)); Fx(false); if (pr) { *v = !*v; Snd(*v ? SFX_ON : SFX_OFF); }
    if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
    float t = st->GetFloat(gid, *v ? 1.f : 0.f); t = Lerp(t, *v ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 14.f)); st->SetFloat(gid, t);
    ImVec2 a(pos.x + 2, pos.y + 3), b(pos.x + 50, pos.y + 25);
    d->AddRectFilled(a, b, Col(ACCENT, 0.92f * t), 11.f);
    d->AddRectFilled(a, b, Col(g_P.panel2, 1.f - t), 11.f);
    d->AddRect(a, b, Col(LerpColor(g_P.dim, ACCENT, t), 0.55f + 0.45f * t), 11.f, 0, 1.2f);
    d->AddCircleFilled(ImVec2(a.x + 11.f + t * 26.f, a.y + 11.f), 7.f, Col(LerpColor(g_P.dim, INK, t)), 24);
    return pr;
}

static const char* g_Wallets[3] = {
    "UQCr0pbOI2BPRzTuZleyp0YBEHGvE-FCsSJLTiHZzy75Tj_4",
    "UQCr0pbOI2BPRzTuZleyp0YBEHGvE-FCsSJLTiHZzy75Tj_4",
    "bc1qeet3kscyqq9dhvlw6e4j4ztaxmg3hh7wdxhqp0"
};

struct Ctx { ImDrawList* d; ImVec2 o; ImVec2 sz; float time; float dt; };

// Ползунок FPS: тонкая «линейка» с делениями и вертикальной ручкой
static void FpsSlider(ImVec2 pos, float w, int* v, int lo, int hi, bool disabled) {
    ImDrawList* d = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(ImVec2(pos.x - 6.f, pos.y - 14.f));
    ImGui::InvisibleButton("##fps", ImVec2(w + 12.f, 28.f));
    bool act = ImGui::IsItemActive() && !disabled, hov = ImGui::IsItemHovered() && !disabled;
    if (hov || act) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (act) {
        float t = std::clamp((ImGui::GetIO().MousePos.x - pos.x) / w, 0.f, 1.f);
        int nv = lo + (int)floorf(t * (hi - lo) + 0.5f); nv = ((nv + 5) / 10) * 10; *v = std::clamp(nv, lo, hi);
    }
    ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
    float hv = st->GetFloat(gid, 0.f); hv = Lerp(hv, (hov || act) ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 12.f)); st->SetFloat(gid, hv);
    float al = disabled ? 0.45f : 1.f;
    float t = (float)(*v - lo) / (hi - lo), hx = pos.x + t * w;
    // деления
    for (int f = lo; f <= hi; f += 20) {
        float x = pos.x + (float)(f - lo) / (hi - lo) * w; bool major = (f % 100 == 0);
        bool on = x <= hx;
        d->AddLine(ImVec2(x, pos.y + 6.f), ImVec2(x, pos.y + (major ? 14.f : 10.f)), on ? Col(AccentT(), (major ? 0.8f : 0.45f) * al) : Col(g_P.dim, major ? 0.5f : 0.25f), 1.f);
    }
    d->AddLine(ImVec2(pos.x, pos.y), ImVec2(pos.x + w, pos.y), Col(g_P.text, 0.16f), 2.f);
    d->AddLine(ImVec2(pos.x, pos.y), ImVec2(hx, pos.y), Col(ACCENT, al), 2.5f);
    for (int i = 1; i <= 8; ++i) d->AddCircleFilled(ImVec2(hx, pos.y), 6.f + i * 2.2f, Col(ACCENT, 0.014f * hv * al), 24);
    d->AddRectFilled(ImVec2(hx - 2.f, pos.y - 9.f - hv * 1.5f), ImVec2(hx + 2.f, pos.y + 9.f + hv * 1.5f), Col(LerpColor(ACCENT, ACCENT_HI, hv), al), 2.f);
}

// ============================================================================
//  Чёрная дыра: тень, фотонное кольцо, аккреционный диск и линзированные дуги
// ============================================================================
static ImVec4 DiskColor(float u) {
    const ImVec4 hot = Rgb(255, 246, 226);
    if (u < 0.30f) return LerpColor(hot, ACCENT_HI, u / 0.30f);
    return LerpColor(ACCENT_HI, ACCENT, std::min(1.f, (u - 0.30f) / 0.5f));
}

// kind: 0 — задняя половина плоского диска, 1 — передняя, 2 — верхняя линзированная дуга, 3 — нижняя (слабая)
static void DiskPass(ImDrawList* d, ImVec2 c, float R, float t, float energy, int kind, float tilt) {
    const int rings = 26, segs = 30; const float PI = 3.14159265f;
    float ct = cosf(tilt), st = sinf(tilt);
    for (int k = 0; k < rings; ++k) {
        float u01 = k / (float)(rings - 1);
        float u = 1.10f + u01 * 1.55f;
        float rx = R * u, ry; bool lower;
        switch (kind) {
        case 0: ry = rx * 0.19f; lower = false; break;
        case 1: ry = rx * 0.19f; lower = true; break;
        case 2: ry = R * (1.0f + (u - 1.0f) * 0.60f); lower = false; break;
        default: ry = R * (1.0f + (u - 1.0f) * 0.13f); lower = true; break;
        }
        float a0 = lower ? 0.f : PI;
        float speed = 1.9f / (0.35f + u01 * 1.4f) * (0.6f + 0.8f * energy);            // дифференциальное вращение
        float jit = 0.80f + 0.40f * Hash01(k * 17 + 3);
        float base = powf(1.f - u01, 1.25f) * jit * (kind == 3 ? 0.42f : (kind == 2 ? 0.95f : 1.f)) * (0.62f + 0.38f * energy);
        float th = 1.5f + (1.f - u01) * 1.4f;
        ImVec4 col = DiskColor(u01);
        ImVec2 prev(0, 0);
        for (int s = 0; s <= segs; ++s) {
            float a = a0 + PI * s / segs;
            float x = cosf(a) * rx, y = sinf(a) * ry;
            ImVec2 p(c.x + x * ct - y * st, c.y + x * st + y * ct);
            if (s > 0) {
                float am = a - PI / segs * 0.5f;
                float beam = 0.34f + 0.66f * (0.5f - 0.5f * cosf(am));                  // доплеровское усиление: левая сторона ярче
                float sw = 0.70f + 0.30f * sinf(am * 5.f + u * 6.f - t * speed);
                d->AddLine(prev, p, Col(col, base * beam * sw), th);
            }
            prev = p;
        }
    }
}

static void DrawBlackHole(ImDrawList* d, ImVec2 c, float R, float t, float energy, float glowK = 4.4f) {
    const float PI = 3.14159265f;
    float tilt = -0.20f + sinf(t * 0.25f) * 0.02f;
    Glow(d, c, R * glowK, ACCENT, (0.12f + 0.10f * energy) * (1.f - g_ThemeAnim * 0.5f), 40);
    DiskPass(d, c, R, t, energy, 0, tilt);
    DiskPass(d, c, R, t, energy, 2, tilt);
    DiskPass(d, c, R, t, energy, 3, tilt);
    d->AddCircleFilled(c, R * 1.02f, Col(Rgb(3, 3, 4)), 96);                            // тень
    for (int i = 6; i >= 1; --i) d->AddCircle(c, R * 1.02f + i * 1.5f, Col(ACCENT_HI, 0.05f * (7 - i) / 6.f), 96, 1.6f);
    for (int i = 0; i < 96; ++i) {                                                       // фотонное кольцо
        float a0 = i * 2.f * PI / 96.f, a1 = (i + 1) * 2.f * PI / 96.f, am = (a0 + a1) * 0.5f;
        float beam = 0.30f + 0.70f * (0.5f - 0.5f * cosf(am - tilt));
        d->AddLine(ImVec2(c.x + cosf(a0) * R * 1.02f, c.y + sinf(a0) * R * 1.02f), ImVec2(c.x + cosf(a1) * R * 1.02f, c.y + sinf(a1) * R * 1.02f),
            Col(Rgb(255, 240, 214), (0.40f + 0.25f * energy) * beam + 0.10f), 1.5f);
    }
    DiskPass(d, c, R, t, energy, 1, tilt);
}
// ============================================================================
//  Свой выбор файла (вместо системного окна)
// ============================================================================
struct FsEntry { std::string name; bool dir; uint64_t size; };
static bool g_PickerOpen = false;
static float g_PickerA = 0.f, g_PickScroll = 0.f, g_PickScrollT = 0.f;
static std::filesystem::path g_PickDir;
static std::vector<FsEntry> g_PickEntries;
static std::string g_PickErr;

static std::string PathStr(const std::filesystem::path& p) { auto u = p.u8string(); return std::string(u.begin(), u.end()); }
static std::filesystem::path PathFromStr(const std::string& s) { return std::filesystem::u8path(s); }
static std::string EnvVar(const wchar_t* n) { wchar_t b[MAX_PATH]; DWORD k = GetEnvironmentVariableW(n, b, MAX_PATH); return (k > 0 && k < MAX_PATH) ? WStringToString(b) : std::string(); }

static void PickerLoad(const std::filesystem::path& dir) {
    g_PickEntries.clear(); g_PickErr.clear(); g_PickDir = dir; g_PickScroll = g_PickScrollT = 0.f;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
    if (ec) { g_PickErr = "Нет доступа к папке"; return; }
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        std::error_code e2; const auto& p = it->path(); std::string nm = PathStr(p.filename());
        if (nm.empty() || nm[0] == '.' || nm[0] == '$') continue;
        bool isDir = it->is_directory(e2);
        if (isDir) { g_PickEntries.push_back({ nm, true, 0 }); continue; }
        std::string ext = PathStr(p.extension()); for (auto& ch : ext) ch = (char)tolower((unsigned char)ch);
        if (ext == ".mp4" || ext == ".mov" || ext == ".m4v") { uint64_t sz = (uint64_t)std::filesystem::file_size(p, e2); g_PickEntries.push_back({ nm, false, e2 ? 0 : sz }); }
    }
    std::sort(g_PickEntries.begin(), g_PickEntries.end(), [](const FsEntry& a, const FsEntry& b) {
        if (a.dir != b.dir) return a.dir;
        std::string x = a.name, y = b.name; for (auto& c : x) c = (char)tolower((unsigned char)c); for (auto& c : y) c = (char)tolower((unsigned char)c); return x < y; });
}
static void OpenPicker() {
    std::filesystem::path start; std::error_code ec;
    if (!g_SelectedFilePath.empty()) start = PathFromStr(DirOf(g_SelectedFilePath));
    if (start.empty() || !std::filesystem::is_directory(start, ec)) {
        std::string up = EnvVar(L"USERPROFILE");
        if (!up.empty()) { start = PathFromStr(up + "\\Videos"); if (!std::filesystem::is_directory(start, ec)) start = PathFromStr(up); }
        else start = "C:\\";
    }
    PickerLoad(start); g_PickerOpen = true; Snd(SFX_PICK);
}

// Вызывать после отрисовки страниц; рисует затемнение и окно выбора
static void DrawFilePicker(ImDrawList* d, ImVec2 wp, ImVec2 ws, float dt) {
    g_PickerA = Lerp(g_PickerA, g_PickerOpen ? 1.f : 0.f, std::min(1.f, dt * 12.f));
    if (g_PickerA < 0.01f) return;
    float aPrev = g_A; g_A = Smooth(g_PickerA);
    const float TB = 44.f;
    ImVec2 full0(wp.x, wp.y + TB), full1(wp.x + ws.x, wp.y + ws.y);
    d->AddRectFilled(full0, full1, Col(Rgb(2, 3, 5), 0.72f));
    float pw = std::min(780.f, ws.x - 140.f), ph = std::min(580.f, ws.y - TB - 80.f);
    ImVec2 a(wp.x + (ws.x - pw) * 0.5f, wp.y + TB + (ws.y - TB - ph) * 0.5f + (1.f - g_A) * 16.f), b(a.x + pw, a.y + ph);
    for (int i = 1; i <= 14; ++i) d->AddRectFilled(ImVec2(a.x - i, a.y - i + 6), ImVec2(b.x + i, b.y + i + 6), Col(ImVec4(0, 0, 0, 0.05f), 1.f - i / 15.f), 6.f + i);
    d->AddRectFilled(a, b, Col(g_P.solid), 5.f);
    Panel(d, a, b);
    // клик по затемнению (вне окна, ниже шапки) закрывает выбор
    {
        ImVec2 mp0 = ImGui::GetIO().MousePos; bool inside = mp0.x >= a.x && mp0.x <= b.x && mp0.y >= a.y && mp0.y <= b.y;
        if (g_PickerOpen && ImGui::IsMouseClicked(0) && !inside && mp0.y > full0.y && mp0.x >= full0.x && mp0.x <= full1.x) { g_PickerOpen = false; Snd(SFX_CLICK); }
    }

    Eyebrow(d, ImVec2(a.x + 24, a.y + 18), "—", "ВЫБОР ВИДЕО");
    // закрыть
    {
        ImVec2 xp(b.x - 44, a.y + 10); ImGui::SetCursorScreenPos(xp);
        bool xc = Fx(ImGui::InvisibleButton("##pickx", ImVec2(30, 30))); bool xh = ImGui::IsItemHovered();
        if (xh) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); d->AddRectFilled(xp, ImVec2(xp.x + 30, xp.y + 30), Col(Rgb(232, 70, 80), 0.2f), 4.f); }
        ImU32 xcol = xh ? Col(Rgb(255, 110, 116)) : Col(g_P.dim);
        d->AddLine(ImVec2(xp.x + 10, xp.y + 10), ImVec2(xp.x + 20, xp.y + 20), xcol, 1.6f); d->AddLine(ImVec2(xp.x + 20, xp.y + 10), ImVec2(xp.x + 10, xp.y + 20), xcol, 1.6f);
        if (xc) g_PickerOpen = false;
    }
    if (g_PickerOpen && ImGui::IsKeyPressed(ImGuiKey_Escape)) g_PickerOpen = false;

    // быстрые места: диски и папки пользователя
    float qx = a.x + 24, qy = a.y + 50;
    {
        struct Q { std::string label, path; };
        std::vector<Q> qs;
        DWORD mask = GetLogicalDrives();
        for (int i = 0; i < 26; ++i) if (mask & (1u << i)) { char l[4] = { (char)('A' + i), ':', 0, 0 }; qs.push_back({ l, std::string(l) + "\\" }); }
        std::string up = EnvVar(L"USERPROFILE");
        if (!up.empty()) { qs.push_back({ "ВИДЕО", up + "\\Videos" }); qs.push_back({ "СТОЛ", up + "\\Desktop" }); qs.push_back({ "ЗАГРУЗКИ", up + "\\Downloads" }); }
        for (size_t i = 0; i < qs.size(); ++i) {
            float w = TrackWidth(g_FontMono, qs[i].label.c_str(), 1.4f) + 26.f;
            if (qx + w > b.x - 24) break;
            char id[24]; snprintf(id, sizeof(id), "##q%d", (int)i);
            if (GhostBtn(id, qs[i].label.c_str(), ImVec2(qx, qy), ImVec2(w, 28), false)) {
                std::error_code ec; auto p = PathFromStr(qs[i].path); if (std::filesystem::is_directory(p, ec)) PickerLoad(p);
            }
            qx += w + 8.f;
        }
    }
    // строка пути + «вверх»
    float py = qy + 40.f;
    {
        std::error_code ec; bool hasParent = g_PickDir.has_parent_path() && g_PickDir.parent_path() != g_PickDir;
        if (GhostBtn("##pickup", "ВВЕРХ", ImVec2(a.x + 24, py), ImVec2(78, 30)) && hasParent) PickerLoad(g_PickDir.parent_path());
        std::string ps = PathStr(g_PickDir);
        d->PushClipRect(ImVec2(a.x + 112, py), ImVec2(b.x - 24, py + 30), true);
        ImVec2 ts = TSize(g_FontSmall, ps.c_str());
        float tx = ts.x > (pw - 136.f) ? (b.x - 24 - ts.x) : (a.x + 112);
        DText(d, g_FontSmall, ImVec2(tx, py + (30 - ts.y) * 0.5f), Col(g_P.dim), ps.c_str());
        d->PopClipRect();
    }

    // список
    ImVec2 la(a.x + 16, py + 42), lb(b.x - 16, b.y - 58);
    d->AddLine(ImVec2(la.x, la.y - 4), ImVec2(lb.x, la.y - 4), Col(g_P.border), 1.f);
    const float RH = 38.f; float total = g_PickEntries.size() * RH, view = lb.y - la.y;
    float maxScroll = std::max(0.f, total - view);
    ImVec2 mp = ImGui::GetIO().MousePos;
    bool inList = g_PickerOpen && mp.x >= la.x && mp.x <= lb.x && mp.y >= la.y && mp.y <= lb.y;
    if (inList) g_PickScrollT = std::clamp(g_PickScrollT - ImGui::GetIO().MouseWheel * 84.f, 0.f, maxScroll);
    g_PickScrollT = std::clamp(g_PickScrollT, 0.f, maxScroll);
    g_PickScroll = Lerp(g_PickScroll, g_PickScrollT, std::min(1.f, dt * 16.f));
    d->PushClipRect(la, lb, true);
    if (!g_PickErr.empty()) DTextC(d, g_FontBody, (la.x + lb.x) * 0.5f, la.y + 40, Col(Rgb(255, 110, 116)), g_PickErr.c_str());
    else if (g_PickEntries.empty()) DTextC(d, g_FontSmall, (la.x + lb.x) * 0.5f, la.y + 40, Col(g_P.dim), "В этой папке нет видео MP4 / MOV");
    int chosen = -1, enter = -1;
    for (int i = 0; i < (int)g_PickEntries.size(); ++i) {
        float y = la.y + i * RH - g_PickScroll;
        if (y + RH < la.y || y > lb.y) continue;
        const FsEntry& e = g_PickEntries[i];
        ImVec2 rp(la.x, y), rs(lb.x - la.x - 10.f, RH - 2.f);
        ImGui::PushID(i); ImGui::SetCursorScreenPos(rp);
        bool clk = ImGui::InvisibleButton("##r", rs); bool hv = ImGui::IsItemHovered(); ImGui::PopID();
        if (hv) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); d->AddRectFilled(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), Col(g_P.panel2), 4.f); }
        if (clk) { if (e.dir) enter = i; else chosen = i; }
        ImU32 ic = e.dir ? Col(AccentT(), 0.9f) : Col(g_P.text, 0.8f);
        ImVec2 ip(rp.x + 14, rp.y + RH * 0.5f - 1.f);
        if (e.dir) {   // папка
            d->AddRect(ImVec2(ip.x, ip.y - 6), ImVec2(ip.x + 18, ip.y + 7), ic, 2.f, 0, 1.4f);
            d->AddLine(ImVec2(ip.x, ip.y - 6), ImVec2(ip.x + 6, ip.y - 9), ic, 1.4f); d->AddLine(ImVec2(ip.x + 6, ip.y - 9), ImVec2(ip.x + 10, ip.y - 6), ic, 1.4f);
        }
        else {       // кадр + play
            d->AddRect(ImVec2(ip.x, ip.y - 7), ImVec2(ip.x + 18, ip.y + 7), ic, 2.f, 0, 1.4f);
            d->AddTriangleFilled(ImVec2(ip.x + 7, ip.y - 3), ImVec2(ip.x + 7, ip.y + 3), ImVec2(ip.x + 12, ip.y), ic);
        }
        d->PushClipRect(ImVec2(rp.x + 44, rp.y), ImVec2(rp.x + rs.x - 110, rp.y + rs.y), true);
        DText(d, g_FontBody, ImVec2(rp.x + 44, rp.y + (RH - 2 - TSize(g_FontBody, "A").y) * 0.5f), Col(g_P.text), e.name.c_str());
        d->PopClipRect();
        if (!e.dir) { std::string sz = FmtSize(e.size); DLabelR(d, rp.x + rs.x - 12, rp.y + 11, hv ? Col(AccentT()) : Col(g_P.dim), sz.c_str(), 1.0f); }
    }
    d->PopClipRect();
    if (maxScroll > 0.f) {   // тонкий скролл
        float th = std::max(30.f, view * view / total), ty = la.y + (view - th) * (g_PickScroll / maxScroll);
        d->AddRectFilled(ImVec2(lb.x - 4, la.y), ImVec2(lb.x - 2, lb.y), Col(g_P.border), 1.f);
        d->AddRectFilled(ImVec2(lb.x - 5, ty), ImVec2(lb.x - 1, ty + th), Col(AccentT(), 0.8f), 2.f);
    }
    if (enter >= 0) { Snd(SFX_CLICK); PickerLoad(g_PickDir / PathFromStr(g_PickEntries[enter].name)); }
    if (chosen >= 0) {
        g_SelectedFilePath = PathStr(g_PickDir / PathFromStr(g_PickEntries[chosen].name));
        g_PickerOpen = false; Snd(SFX_PICK); AddToast("Видео выбрано"); g_Page = 0;
    }

    // низ: подсказка и системный диалог
    d->AddLine(ImVec2(a.x + 16, b.y - 52), ImVec2(b.x - 16, b.y - 52), Col(g_P.border), 1.f);
    DLabel(d, ImVec2(a.x + 24, b.y - 34), Col(g_P.dim), "MP4 · MOV · ESC — ЗАКРЫТЬ", 1.4f);
    if (GhostBtn("##pickos", "СИСТЕМНОЕ ОКНО", ImVec2(b.x - 24 - 156, b.y - 42), ImVec2(156, 30))) {
        g_PickerOpen = false; std::string ch = OpenVideoDialog(g_hwnd); if (!ch.empty()) { g_SelectedFilePath = ch; AddToast("Видео выбрано"); Snd(SFX_PICK); }
    }
    g_A = aPrev;
}
// ============================================================================
//  Страница: Конвертер
// ============================================================================
static float g_Energy = 0.f;
static std::atomic<bool> g_ProgFinish(false);      // ядро закончило — прогресс добегает до 100%
static double g_ProcStartT = 0.0; static float g_ProgShown = 0.f;
static const char* StageName(float p) { return p < 0.18f ? "ЧТЕНИЕ СТРУКТУРЫ" : p < 0.45f ? "ПЕРЕСТРОЙКА ТАБЛИЦ" : p < 0.78f ? "УПАКОВКА ДАННЫХ" : p < 0.98f ? "ФИНАЛИЗАЦИЯ" : "ГОТОВО"; }

static void PageConvert(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f;
    float x0 = c.o.x + m, y0 = c.o.y + 20.f;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "КОНВЕРТЕР", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Оптимизация видео");

    const float orbW = 300.f, gap = 28.f;
    float leftW = c.sz.x - m * 2 - orbW - gap; if (leftW < 400.f) leftW = 400.f;
    float rx = x0 + leftW + gap, top = y0 + 78.f, bottom = c.o.y + c.sz.y - 24.f;

    bool busy = g_IsProcessing; bool hasFile = !g_SelectedFilePath.empty();
    std::vector<DoneItem> done; { std::lock_guard<std::mutex> lock(g_UIMutex); done = g_Done; }

    static std::string lastPath; static uint64_t selSize = 0;
    if (g_SelectedFilePath != lastPath) { lastPath = g_SelectedFilePath; selSize = hasFile ? FileSizeOf(g_SelectedFilePath) : 0; }

    // ---- 01 · Источник ---------------------------------------------------
    ImVec2 da(x0, top), db(x0 + leftW, top + 170);
    Panel(d, da, db);
    if (!hasFile) {
        ImGui::SetCursorScreenPos(da);
        bool clicked = ImGui::InvisibleButton("##drop", ImVec2(leftW, 170));
        bool hov = ImGui::IsItemHovered() && !busy;
        g_DropHover = Lerp(g_DropHover, hov ? 1.f : 0.f, std::min(1.f, c.dt * 10.f));
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        if (clicked && !busy) OpenPicker();

        float cxm = (da.x + db.x) * 0.5f, cym = da.y + 62.f;
        float bw = 130.f + 14.f * g_DropHover, bh = 68.f + 8.f * g_DropHover;
        d->AddRectFilled(ImVec2(da.x + 1, da.y + 1), ImVec2(db.x - 1, db.y - 1), Col(ACCENT, 0.05f * g_DropHover), 4.f);
        Brackets(d, ImVec2(cxm - bw * 0.5f, cym - bh * 0.5f), ImVec2(cxm + bw * 0.5f, cym + bh * 0.5f), 16.f, Col(LerpColor(g_P.dim, ACCENT, g_DropHover), 0.55f + 0.45f * g_DropHover), 1.8f);
        d->AddLine(ImVec2(cxm - 9, cym), ImVec2(cxm + 9, cym), Col(AccentT(), 0.8f), 1.4f);
        d->AddLine(ImVec2(cxm, cym - 9), ImVec2(cxm, cym + 9), Col(AccentT(), 0.8f), 1.4f);
        d->AddCircle(ImVec2(cxm, cym), 4.f + sinf(c.time * 2.4f) * 0.8f, Col(AccentT(), 0.9f), 20, 1.3f);
        if (g_DropHover > 0.02f) {                           // сканирующая линия
            float sy = cym - bh * 0.5f + fmodf(c.time * 70.f, bh);
            d->AddRectFilledMultiColor(ImVec2(cxm - bw * 0.5f + 4, sy), ImVec2(cxm + bw * 0.5f - 4, sy + 1.4f), Col(ACCENT, 0.f), Col(ACCENT, 0.7f * g_DropHover), Col(ACCENT, 0.7f * g_DropHover), Col(ACCENT, 0.f));
        }
        Eyebrow(d, ImVec2(da.x + 22, da.y + 16), "01", "ИСТОЧНИК");
        DTextC(d, g_FontH2, cxm, da.y + 108, Col(g_P.text), "Перетащите видео сюда");
        DLabelC(d, cxm, da.y + 140, Col(g_P.dim), "ИЛИ НАЖМИТЕ ДЛЯ ВЫБОРА  ·  MP4  ·  MOV", 1.6f);
    }
    else {
        Eyebrow(d, ImVec2(da.x + 22, da.y + 16), "01", "ИСТОЧНИК");
        ImVec2 ib(da.x + 24, da.y + 50);
        d->AddRectFilled(ib, ImVec2(ib.x + 92, ib.y + 92), Col(ACCENT, 0.07f), 3.f);
        Brackets(d, ib, ImVec2(ib.x + 92, ib.y + 92), 12.f, Col(AccentT(), 0.9f), 1.6f);
        ImVec2 ic(ib.x + 46, ib.y + 46);
        d->AddTriangle(ImVec2(ic.x - 8, ic.y - 12), ImVec2(ic.x - 8, ic.y + 12), ImVec2(ic.x + 13, ic.y), Col(AccentT()), 1.8f);

        float tx = ib.x + 92 + 24;
        d->PushClipRect(ImVec2(tx, da.y), ImVec2(db.x - 56, db.y), true);
        DText(d, g_FontH2, ImVec2(tx, da.y + 52), Col(g_P.text), FileNameOf(g_SelectedFilePath).c_str());
        std::string info = FmtSize(selSize);
        DLabel(d, ImVec2(tx, da.y + 88), Col(AccentT()), info.c_str(), 1.4f);
        DText(d, g_FontSmall, ImVec2(tx, da.y + 114), Col(g_P.dim), DirOf(g_SelectedFilePath).c_str());
        d->PopClipRect();

        ImVec2 xp(db.x - 44, da.y + 12);
        ImGui::SetCursorScreenPos(xp);
        bool xc = Fx(ImGui::InvisibleButton("##clear", ImVec2(28, 28))); bool xh = ImGui::IsItemHovered();
        if (xh) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        d->AddRectFilled(xp, ImVec2(xp.x + 28, xp.y + 28), Col(Rgb(232, 70, 80), xh ? 0.22f : 0.f), 4.f);
        ImU32 xcol = xh ? Col(Rgb(255, 110, 116)) : Col(g_P.dim);
        d->AddLine(ImVec2(xp.x + 9, xp.y + 9), ImVec2(xp.x + 19, xp.y + 19), xcol, 1.6f);
        d->AddLine(ImVec2(xp.x + 19, xp.y + 9), ImVec2(xp.x + 9, xp.y + 19), xcol, 1.6f);
        if (xc && !busy) g_SelectedFilePath.clear();

        if (!busy && GhostBtn("##replace", "ЗАМЕНИТЬ", ImVec2(db.x - 24 - 112, db.y - 20 - 30), ImVec2(112, 30))) OpenPicker();
    }

    // ---- 02 · FPS --------------------------------------------------------
    ImVec2 fa(x0, db.y + 14), fb(x0 + leftW, db.y + 14 + 158);
    Panel(d, fa, fb);
    Eyebrow(d, ImVec2(fa.x + 22, fa.y + 16), "02", "ЦЕЛЕВОЙ FPS");
    char fpsBuf[16]; snprintf(fpsBuf, sizeof(fpsBuf), "%d", g_TargetFps);
    DText(d, g_FontBig, ImVec2(fa.x + 20, fa.y + 34), Col(AccentT()), fpsBuf);
    ImVec2 fs = TSize(g_FontBig, fpsBuf);
    DLabel(d, ImVec2(fa.x + 30 + fs.x, fa.y + 74), Col(g_P.dim), "КАДР/С");
    const int presets[4] = { 240, 400, 600, 1000 };
    for (int i = 0; i < 4; ++i) {
        char lb[16], idb[16]; snprintf(lb, sizeof(lb), "%d", presets[i]); snprintf(idb, sizeof(idb), "##pf%d", i);
        float cx = fb.x - 22 - (4 - i) * 58.f + 4.f;
        if (TabText(idb, lb, ImVec2(cx, fa.y + 12), 54.f, g_TargetFps == presets[i]) && !busy) g_TargetFps = presets[i];
    }
    FpsSlider(ImVec2(fa.x + 22, fa.y + 118), leftW - 44, &g_TargetFps, 60, 1000, busy);
    DLabel(d, ImVec2(fa.x + 22, fa.y + 136), Col(g_P.dim, 0.8f), "60", 1.2f);
    DLabelR(d, fb.x - 22, fa.y + 136, Col(g_P.dim, 0.8f), "1000", 1.2f);

    // ---- Кнопка запуска --------------------------------------------------
    ImVec2 bp(x0, fb.y + 14); ImVec2 bsz(leftW, 54);
    int bstate = !hasFile ? 0 : (busy ? 2 : 1);
    // фейковый прогресс: быстро растёт и замедляется к ~94%, по готовности добегает до 100%
    float progTarget = 0.f;
    if (busy) { float t = (float)(c.time - g_ProcStartT); progTarget = g_ProgFinish ? 1.f : std::min(0.94f, 1.f - expf(-t / 1.9f)); }
    g_ProgShown = busy ? Lerp(g_ProgShown, progTarget, std::min(1.f, c.dt * 5.f)) : 0.f;
    char runLabel[40]; if (busy) snprintf(runLabel, sizeof(runLabel), "ОБРАБОТКА  %d%%", (int)(g_ProgShown * 100.f + 0.5f));
    if (PrimaryButton("##run", busy ? runLabel : "ОПТИМИЗИРОВАТЬ", bp, bsz, bstate, c.time, g_ProgShown)) {
        g_ProgFinish = false; g_ProcStartT = c.time; g_ProgShown = 0.f;
        g_IsProcessing = true; Snd(SFX_RUN); std::string input = g_SelectedFilePath; int fps = g_TargetFps;
        std::thread([input, fps]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1500 + (rand() % 1000)));
            Qypxr::Result res = Qypxr::OptimizeVideoSparse(input, fps);
            g_ProgFinish = true; std::this_thread::sleep_for(std::chrono::milliseconds(650));   // даём прогрессу «добежать» до 100%
            std::lock_guard<std::mutex> lock(g_UIMutex);
            if (res.success) {
                g_Done.push_back({ res.outputPath, res.originalSize, res.processedSize, res.timeSeconds }); g_Toasts.push_back({ "Оптимизация завершена!", 3.0f }); g_PendingSnd = SFX_OK;
                if (g_AutoOpen) { std::wstring args = L"/select,\"" + StringToWString(res.outputPath) + L"\""; ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL); }
            }
            else { g_Toasts.push_back({ "Ошибка: " + res.message, 3.5f }); g_PendingSnd = SFX_ERR; }
            g_IsProcessing = false;
            }).detach();
    }

    // ---- 03 · Готовые файлы ---------------------------------------------
    ImVec2 ra(x0, bp.y + bsz.y + 14), rb(x0 + leftW, bottom);
    if (rb.y - ra.y < 112.f) rb.y = ra.y + 112.f;
    Panel(d, ra, rb);
    Eyebrow(d, ImVec2(ra.x + 22, ra.y + 16), "03", "ГОТОВЫЕ ФАЙЛЫ");
    { char cnt[24]; snprintf(cnt, sizeof(cnt), "%02d", (int)done.size()); DLabelR(d, rb.x - 22, ra.y + 16, Col(g_P.dim), cnt); }
    if (done.empty()) {
        DTextC(d, g_FontSmall, (ra.x + rb.x) * 0.5f, (ra.y + rb.y) * 0.5f + 10, Col(g_P.dim), "Здесь появятся обработанные видео");
    }
    else {
        int maxRows = std::max(1, (int)((rb.y - ra.y - 56.f) / 52.f));
        for (int k = 0; k < maxRows && k < (int)done.size(); ++k) {
            const DoneItem& it = done[done.size() - 1 - k];
            ImVec2 rp(ra.x + 12, ra.y + 48 + k * 52.f), rs(leftW - 24, 48);
            ImGui::PushID(k);
            ImGui::SetCursorScreenPos(rp);
            bool rc = Fx(ImGui::InvisibleButton("##row", rs)); bool rh = ImGui::IsItemHovered();
            ImGui::PopID();
            if (rh) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            if (rh) d->AddRectFilled(rp, ImVec2(rp.x + rs.x, rp.y + rs.y), Col(g_P.panel2), 4.f);
            if (k > 0) d->AddLine(ImVec2(rp.x + 8, rp.y - 2), ImVec2(rp.x + rs.x - 8, rp.y - 2), Col(g_P.border), 1.f);
            const ImVec4 ok = Rgb(96, 206, 150);
            d->AddCircle(ImVec2(rp.x + 22, rp.y + 24), 10.f, Col(ok, 0.8f), 24, 1.4f);
            Poly3(d, ImVec2(rp.x + 17, rp.y + 24), ImVec2(rp.x + 21, rp.y + 28), ImVec2(rp.x + 28, rp.y + 19), Col(ok), 1.8f);
            d->PushClipRect(ImVec2(rp.x + 44, rp.y), ImVec2(rp.x + rs.x - 170, rp.y + rs.y), true);
            DText(d, g_FontBody, ImVec2(rp.x + 44, rp.y + 4), Col(g_P.text), FileNameOf(it.path).c_str());
            char sub[96]; snprintf(sub, sizeof(sub), "%s → %s  ·  %.2f с", FmtSize(it.orig).c_str(), FmtSize(it.proc).c_str(), it.sec);
            DText(d, g_FontSmall, ImVec2(rp.x + 44, rp.y + 26), Col(g_P.dim), sub);
            d->PopClipRect();
            DLabelR(d, rp.x + rs.x - 14, rp.y + 17, rh ? Col(AccentT()) : Col(g_P.dim), "В ПАПКЕ", 1.4f);
            if (rc) { std::wstring args = L"/select,\"" + StringToWString(it.path) + L"\""; ShellExecuteW(NULL, L"open", L"explorer.exe", args.c_str(), NULL, SW_SHOWNORMAL); }
        }
    }

    // ---- Правая колонка: сингулярность ----------------------------------
    g_Energy = Lerp(g_Energy, busy ? 1.f : 0.f, std::min(1.f, c.dt * 2.2f));
    ImVec2 oa(rx, top - 6), ob(rx + orbW, top + 294);
    ImVec2 ctr(oa.x + orbW * 0.5f, oa.y + 146.f);
    d->PushClipRect(ImVec2(oa.x - 14, oa.y), ImVec2(ob.x + 14, ob.y + 6), true);
    float R = 46.f + sinf(c.time * 1.3f) * 0.8f;
    DrawBlackHole(d, ctr, R, c.time, g_Energy, 3.1f);   // свечение не шире колонки — без жёстких краёв обрезки
    if (busy) {
        if (g_ParticlesOn) InitParticles();
        for (auto& p : g_Particles) {
            p.angle += p.speed * 40.f * c.dt; p.radius -= p.speed * 1100.f * c.dt; p.alpha -= 110.f * c.dt;
            if (p.radius < R * 1.1f || p.alpha <= 0) { p.angle = (rand() % 360) * 3.14159f / 180.f; p.radius = p.base_radius * 0.78f + (rand() % 30); p.alpha = 255.f; }
            auto proj = [&](float ang, float rad) { return ImVec2(ctr.x + cosf(ang) * rad, ctr.y + sinf(ang) * rad * 0.28f); };
            ImVec2 pp = proj(p.angle, p.radius), tail = proj(p.angle - 0.14f, p.radius + 5.f);
            float al = p.alpha / 255.f;
            d->AddLine(pp, tail, Col(ACCENT_HI, al * 0.45f), 1.2f);
            d->AddCircleFilled(pp, p.size * 0.55f, Col(Rgb(255, 240, 214), al), 8);
        }
    }
    else g_Particles.clear();
    d->PopClipRect();

    const char* st = busy ? StageName(g_ProgShown) : (hasFile ? "ГОТОВ К ЗАПУСКУ" : "ОЖИДАНИЕ ФАЙЛА");
    float stw = TrackWidth(g_FontMono, st, 2.2f), scx = rx + orbW * 0.5f;
    float pulse = busy ? 0.5f + 0.5f * sinf(c.time * 7.f) : (hasFile ? 1.f : 0.35f + 0.25f * sinf(c.time * 1.8f));
    d->AddCircleFilled(ImVec2(scx - stw * 0.5f - 12, top + 322), 3.f, Col(busy || hasFile ? ACCENT : g_P.dim, pulse), 12);
    DLabel(d, ImVec2(scx - stw * 0.5f, top + 314), busy ? Col(AccentT()) : Col(g_P.text), st, 2.2f);
    if (busy) {   // тонкая шкала прогресса под статусом
        float bw = 220.f, bx = scx - bw * 0.5f, by = top + 338.f;
        d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 2.f), Col(g_P.text, 0.16f), 1.f);
        d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * g_ProgShown, by + 2.f), Col(ACCENT), 1.f);
        for (int i = 1; i <= 5; ++i) d->AddCircleFilled(ImVec2(bx + bw * g_ProgShown, by + 1.f), 2.f + i * 2.f, Col(ACCENT, 0.05f), 16);
        char pc[16]; snprintf(pc, sizeof(pc), "%d%%", (int)(g_ProgShown * 100.f + 0.5f));
        DLabelR(d, bx + bw + 44.f, by - 6.f, Col(g_P.dim), pc, 1.0f);
    }

    // ---- Телеметрия ---------------------------------------------------------
    ImVec2 sa(rx, top + 346), sb(rx + orbW, top + 346 + 96);
    Panel(d, sa, sb);
    DLabel(d, ImVec2(sa.x + 18, sa.y + 14), Col(g_P.dim), "ОБРАБОТАНО");
    { char n[16]; snprintf(n, sizeof(n), "%02d", (int)done.size()); DTextR(d, g_FontH2, sb.x - 18, sa.y + 8, Col(AccentT()), n); }
    d->AddLine(ImVec2(sa.x + 18, sa.y + 40), ImVec2(sb.x - 18, sa.y + 40), Col(g_P.border), 1.f);
    DLabel(d, ImVec2(sa.x + 18, sa.y + 50), Col(g_P.dim), "ПОСЛЕДНИЙ");
    if (done.empty()) DText(d, g_FontBody, ImVec2(sa.x + 18, sa.y + 68), Col(g_P.dim), "—");
    else {
        const DoneItem& l = done.back(); char b2[96]; snprintf(b2, sizeof(b2), "%s → %s", FmtSize(l.orig).c_str(), FmtSize(l.proc).c_str());
        DText(d, g_FontBody, ImVec2(sa.x + 18, sa.y + 68), Col(g_P.text), b2);
    }

    ImVec2 ta(rx, sb.y + 14), tb(rx + orbW, bottom);
    if (tb.y - ta.y > 84.f) {
        Panel(d, ta, tb);
        Eyebrow(d, ImVec2(ta.x + 18, ta.y + 14), "—", "БЫСТРЫЙ СТАРТ");
        DrawWrapped(d, g_FontSmall, ImVec2(ta.x + 18, ta.y + 38), orbW - 36, Col(g_P.dim),
            "Перетащите файл в окно или выберите кнопкой. Результат появится рядом с исходным видео.", 19.f);
    }
}

// ============================================================================
//  Страница: Настройки
// ============================================================================
static void PageSettings(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f;
    float W = std::min(c.sz.x - m * 2, 1180.f);
    float x0 = c.o.x + (c.sz.x - W) * 0.5f;
    float extra = std::max(0.f, c.sz.y - 650.f);
    float y0 = c.o.y + 20.f + extra * 0.4f;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "СИСТЕМА", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Настройки");
    float lw = W * 0.52f, rw = W - lw - 16.f, top = y0 + 78.f, ease = Smooth(g_ThemeAnim);

    // ---- 01 · Тема: ночь ↔ рассвет ----
    ImVec2 a(x0, top), b(x0 + lw, top + 218);
    Panel(d, a, b);
    ImVec2 sa(a.x + 14, a.y + 14), sb(b.x - 14, a.y + 152);
    float horizon = sb.y - 24.f, scx = (sa.x + sb.x) * 0.5f;
    {   // небо: вертикальный градиент между ночью и рассветом
        ImU32 topN = Col(Rgb(6, 8, 20)), botN = Col(Rgb(26, 24, 46)), topD = Col(Rgb(150, 160, 190)), botD = Col(Rgb(255, 196, 150));
        ImU32 t0 = ImGui::ColorConvertFloat4ToU32(LerpColor(Rgb(6, 8, 20), Rgb(132, 150, 190), ease)), t1 = ImGui::ColorConvertFloat4ToU32(LerpColor(Rgb(30, 26, 50), Rgb(255, 205, 160), ease));
        (void)topN; (void)botN; (void)topD; (void)botD;
        d->AddRectFilledMultiColor(sa, ImVec2(sb.x, horizon), t0, t0, t1, t1);
    }
    d->PushClipRect(sa, ImVec2(sb.x, horizon), true);
    for (int k = 0; k < 22; ++k) {
        float sx = sa.x + fmodf(k * 53.7f + 11.f, sb.x - sa.x), sy = sa.y + fmodf(k * 31.3f + 7.f, 70.f);
        d->AddCircleFilled(ImVec2(sx, sy), 1.1f + (k % 3) * 0.3f, Col(WHITE, (1.f - ease) * (0.35f + 0.5f * sinf(c.time * 1.6f + k))));
    }
    float R = 74.f;
    float moonA = -3.14159f / 2.f + ease * 3.14159f, sunA = 3.14159f / 2.f + ease * 3.14159f;
    ImVec2 moon(scx + R * cosf(moonA), horizon + R * sinf(moonA)), sun(scx + R * cosf(sunA), horizon + R * sinf(sunA));
    for (int i = 10; i > 0; --i) d->AddCircleFilled(sun, 16.f + i * 4.f, Col(Rgb(255, 170, 90), 0.045f), 40);
    d->AddCircleFilled(sun, 16.f, Col(Rgb(255, 232, 190)), 40);
    for (int i = 8; i > 0; --i) d->AddCircleFilled(moon, 14.f + i * 2.5f, Col(Rgb(170, 190, 255), 0.035f), 40);
    d->AddCircleFilled(moon, 14.f, Col(Rgb(226, 230, 240)), 40);
    d->AddCircleFilled(ImVec2(moon.x - 4, moon.y - 3), 3.f, Col(Rgb(190, 196, 212)), 16);
    d->AddCircleFilled(ImVec2(moon.x + 4, moon.y + 4), 2.2f, Col(Rgb(190, 196, 212)), 16);
    d->PopClipRect();
    d->AddRectFilled(ImVec2(sa.x, horizon), sb, Col(LerpColor(Rgb(5, 6, 10), Rgb(70, 60, 56), ease)), 3.f, ImDrawFlags_RoundCornersBottom);
    d->AddLine(ImVec2(sa.x, horizon), ImVec2(sb.x, horizon), Col(LerpColor(Rgb(255, 200, 150), Rgb(255, 235, 200), ease), 0.55f), 1.f);
    Eyebrow(d, ImVec2(sa.x + 10, sa.y + 8), "01", "ТЕМА");

    DText(d, g_FontBody, ImVec2(a.x + 20, a.y + 164), Col(g_P.text), "Тема оформления");
    DLabel(d, ImVec2(a.x + 20, a.y + 190), Col(g_P.dim), g_IsLightTheme ? "РАССВЕТ  ·  СВЕТЛАЯ" : "НОЧЬ  ·  ТЁМНАЯ", 1.6f);
    if (Switch("##theme", ImVec2(b.x - 20 - 52, a.y + 172), &g_IsLightTheme)) { AddToast(g_IsLightTheme ? "Светлая тема включена" : "Тёмная тема включена"); SaveSettings(); }

    // ---- 02 · Акцент ----
    ImVec2 a2(x0, b.y + 14), b2(x0 + lw, b.y + 14 + 128);
    Panel(d, a2, b2);
    Eyebrow(d, ImVec2(a2.x + 20, a2.y + 16), "02", "АКЦЕНТНЫЙ ЦВЕТ");
    float sp = (lw - 72.f) / 4.f;
    for (int i = 0; i < 5; ++i) {
        ImVec2 cc(a2.x + 36 + i * sp, a2.y + 64);
        char id[16]; snprintf(id, sizeof(id), "##ac%d", i);
        ImGui::SetCursorScreenPos(ImVec2(cc.x - 20, cc.y - 20));
        if (Fx(ImGui::InvisibleButton(id, ImVec2(40, 40)))) { g_AccentIdx = i; SaveSettings(); }
        bool hv = ImGui::IsItemHovered(); if (hv) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        bool sel = (g_AccentIdx == i);
        if (sel) for (int k = 1; k <= 6; ++k) d->AddCircleFilled(cc, 13.f + k * 2.f, Col(g_Accents[i].a, 0.035f), 32);
        d->AddCircleFilled(cc, (hv || sel) ? 13.f : 11.5f, Col(g_Accents[i].a), 32);
        d->AddCircleFilled(ImVec2(cc.x - 3, cc.y - 3), 4.f, Col(WHITE, 0.18f), 16);
        if (sel) d->AddCircle(cc, 20.f, Col(g_P.text, 0.9f), 40, 1.2f);
    }
    DLabel(d, ImVec2(a2.x + 20, a2.y + 100), Col(g_P.dim), "АКТИВЕН:", 1.6f);
    DText(d, g_FontSmall, ImVec2(a2.x + 98, a2.y + 98), Col(AccentT()), g_Accents[g_AccentIdx].name);

    // ---- 03 · Параметры ----
    ImVec2 ra(x0 + lw + 16, top), rb(x0 + W, top + 380);
    Panel(d, ra, rb);
    Eyebrow(d, ImVec2(ra.x + 20, ra.y + 16), "03", "ПАРАМЕТРЫ");
    struct Row { const char* t; const char* s; bool* v; };
    Row rows[6] = {
        { "Анимированный фон", "Звёзды, туманность и метеоры", &g_AnimBg },
        { "Частицы", "Эффект во время обработки", &g_ParticlesOn },
        { "Уведомления", "Всплывающие сообщения", &g_ToastsOn },
        { "Открывать папку", "После завершения обработки", &g_AutoOpen },
        { "Запоминать FPS", "Сохранять между запусками", &g_RememberFps },
        { "Звуки интерфейса", "Клики, уведомления, завершение", &g_SoundOn }
    };
    for (int i = 0; i < 6; ++i) {
        float ry = ra.y + 46 + i * 54.f;
        DText(d, g_FontBody, ImVec2(ra.x + 20, ry), Col(g_P.text), rows[i].t);
        DText(d, g_FontSmall, ImVec2(ra.x + 20, ry + 22), Col(g_P.dim), rows[i].s);
        char id[16]; snprintf(id, sizeof(id), "##sw%d", i);
        if (Switch(id, ImVec2(rb.x - 20 - 52, ry + 6), rows[i].v)) SaveSettings();
        if (i < 5) d->AddLine(ImVec2(ra.x + 20, ry + 46), ImVec2(rb.x - 20, ry + 46), Col(g_P.border), 1.f);
    }

    // ---- 04 · Цитата ----
    ImVec2 qa(x0, top + 380 + 14), qb(x0 + W, top + 380 + 14 + 150);
    Panel(d, qa, qb);
    DText(d, g_FontBig, ImVec2(qa.x + 22, qa.y + 6), Col(ACCENT, 0.55f), "«");
    Eyebrow(d, ImVec2(qa.x + 76, qa.y + 16), "04", "ЦИТАТА ДНЯ");
    std::string q = g_Quotes[g_CurrentQuoteIdx].first;
    DrawWrapped(d, g_FontH2, ImVec2(qa.x + 76, qa.y + 44), W - 110, Col(g_P.text), q, 28.f);
    std::string au = g_Quotes[g_CurrentQuoteIdx].second;
    DText(d, g_FontSmall, ImVec2(qa.x + 76, qb.y - 34), Col(AccentT()), au.c_str());
    if (GhostBtn("##nextq", "ДРУГАЯ ЦИТАТА", ImVec2(qb.x - 24 - 150, qb.y - 42), ImVec2(150, 30))) g_CurrentQuoteIdx = (g_CurrentQuoteIdx + 1) % (int)g_Quotes.size();
    (void)rw;
}

// ============================================================================
//  Страница: Помощь
// ============================================================================
static void PageHelp(const Ctx& c) {
    ImDrawList* d = c.d; const float m = 32.f;
    float W = std::min(c.sz.x - m * 2, 1180.f);
    float x0 = c.o.x + (c.sz.x - W) * 0.5f;
    float extra = std::max(0.f, c.sz.y - 700.f);
    float heroExtra = std::min(extra * 0.55f, 260.f);
    float y0 = c.o.y + 20.f + (extra - heroExtra) * 0.4f;
    DLabel(d, ImVec2(x0, y0), Col(AccentT()), "О ПРОГРАММЕ", 2.4f);
    DText(d, g_FontH1, ImVec2(x0, y0 + 14), Col(g_P.text), "Помощь");

    // герой: чёрная дыра справа, название слева
    ImVec2 a(x0, y0 + 78), b(x0 + W, y0 + 78 + 190 + heroExtra);
    Panel(d, a, b);
    d->PushClipRect(a, b, true);
    float hh = b.y - a.y;
    ImVec2 ctr(a.x + W * 0.70f, (a.y + b.y) * 0.5f);
    float R = std::min(62.f, hh * 0.24f) * std::max(1.f, std::min(W / 900.f, 1.35f));
    DrawBlackHole(d, ctr, R, c.time, 0.35f);
    d->PopClipRect();
    DText(d, g_FontBig, ImVec2(a.x + 32, a.y + hh * 0.5f - 62), Col(g_P.text), "QYPXR");
    DTrack(d, g_FontH2, ImVec2(a.x + 36, a.y + hh * 0.5f + 4), Col(AccentT()), "METHOD", 6.f);
    DLabel(d, ImVec2(a.x + 36, a.y + hh * 0.5f + 40), Col(g_P.dim), "ОПТИМИЗАТОР ВИДЕО ДЛЯ TIKTOK  ·  V2.0", 1.6f);

    // инструкция
    float lw = W * 0.6f - 8.f;
    ImVec2 sa(x0, b.y + 16), sb(x0 + lw, b.y + 16 + 180);
    Panel(d, sa, sb);
    Eyebrow(d, ImVec2(sa.x + 22, sa.y + 16), "01", "КАК ПОЛЬЗОВАТЬСЯ");
    const char* steps[3] = { "Выберите или перетащите MP4/MOV файл", "Задайте целевой FPS — по умолчанию 400", "Нажмите «Оптимизировать». Рядом появится файл с суффиксом _QYPXR" };
    for (int i = 0; i < 3; ++i) {
        float yy = sa.y + 48 + i * 42.f;
        char n[4]; snprintf(n, sizeof(n), "0%d", i + 1);
        DLabel(d, ImVec2(sa.x + 22, yy + 4), Col(AccentT()), n, 1.5f);
        d->AddLine(ImVec2(sa.x + 54, yy + 12), ImVec2(sa.x + 66, yy + 12), Col(g_P.dim, 0.5f), 1.f);
        DrawWrapped(d, g_FontSmall, ImVec2(sa.x + 78, yy + 3), lw - 100, Col(g_P.text), steps[i], 18.f);
    }

    // контакты
    ImVec2 ca(sb.x + 16, b.y + 16), cb(x0 + W, b.y + 16 + 180);
    Panel(d, ca, cb);
    Eyebrow(d, ImVec2(ca.x + 22, ca.y + 16), "02", "КОНТАКТЫ");
    float bw = cb.x - ca.x - 44;
    if (LinkBtn("##tg", "Telegram  @qypxr1", ImVec2(ca.x + 22, ca.y + 50), ImVec2(bw, 44), Rgb(70, 200, 230))) OpenUrl(L"https://t.me/qypxr1");
    if (LinkBtn("##tt", "TikTok  @qypxr1", ImVec2(ca.x + 22, ca.y + 106), ImVec2(bw, 44), Rgb(240, 98, 150))) OpenUrl(L"https://www.tiktok.com/@qypxr1");

    // донаты
    ImVec2 da(x0, sb.y + 16), db(x0 + W, sb.y + 16 + 160);
    Panel(d, da, db);
    Eyebrow(d, ImVec2(da.x + 22, da.y + 16), "03", "ПОДДЕРЖАТЬ ПРОЕКТ");
    const char* names[3] = { "USDT · TON", "GRAM · TON", "Bitcoin · BTC" };
    ImVec4 dots[3] = { Rgb(38, 161, 123), Rgb(52, 152, 255), Rgb(247, 147, 26) };
    for (int i = 0; i < 3; ++i) {
        float ry = da.y + 46 + i * 36.f;
        d->AddCircleFilled(ImVec2(da.x + 30, ry + 15), 4.f, Col(dots[i]), 16);
        d->AddCircle(ImVec2(da.x + 30, ry + 15), 8.f, Col(dots[i], 0.4f), 20, 1.f);
        DText(d, g_FontBody, ImVec2(da.x + 46, ry + 5), Col(g_P.text), names[i]);
        float chipX = db.x - 22 - 118;
        d->PushClipRect(ImVec2(da.x + 170, ry), ImVec2(chipX - 10, ry + 32), true);
        DLabel(d, ImVec2(da.x + 170, ry + 8), Col(g_P.dim), g_Wallets[i], 0.6f);
        d->PopClipRect();
        char id[16]; snprintf(id, sizeof(id), "##cp%d", i);
        if (GhostBtn(id, "КОПИРОВАТЬ", ImVec2(chipX, ry + 2), ImVec2(118, 28))) { ImGui::SetClipboardText(g_Wallets[i]); AddToast("Адрес скопирован"); }
    }
}

// ============================================================================
//  Каркас: атмосфера, шапка, сайдбар, тосты
// ============================================================================
static void DrawBackground(ImDrawList* d, ImVec2 wp, ImVec2 ws, float t, float dt) {
    ImVec2 e(wp.x + ws.x, wp.y + ws.y);
    d->AddRectFilled(wp, e, Col(g_P.bg));
    float dk = 1.f - g_ThemeAnim * 0.55f, night = 1.f - g_ThemeAnim;
    float tt = g_AnimBg ? t : 0.f;
    // туманность: тёплое ядро справа сверху, слабый отсвет по центру и холодная глубина слева снизу
    Glow(d, ImVec2(wp.x + ws.x * 0.80f + sinf(tt * 0.13f) * 50.f, wp.y + 150.f), 480.f, ACCENT, 0.17f * dk, 48);
    Glow(d, ImVec2(wp.x + ws.x * 0.58f, wp.y + ws.y * 0.58f + cosf(tt * 0.11f) * 30.f), 300.f, ACCENT, 0.045f * dk, 32);
    Glow(d, ImVec2(wp.x + 160.f, wp.y + ws.y - 60.f + cosf(tt * 0.17f) * 24.f), 400.f, Rgb(40, 92, 160), 0.11f * dk, 40);
    if (g_AnimBg) {
        InitStars();
        for (auto& s : g_Stars) {
            float x = fmodf(s.x + t * 0.0016f * s.size, 1.f);
            float al = (0.28f + 0.30f * sinf(t * s.speed + s.phase)) * night;
            ImVec2 p(wp.x + x * ws.x, wp.y + s.y * ws.y);
            d->AddCircleFilled(p, s.size * 0.75f, Col(Rgb(255, 244, 228), al * (0.4f + s.size * 0.35f)), 8);
            if (s.size > 1.55f) {   // редкие крупные звёзды получают крестообразный блик
                d->AddLine(ImVec2(p.x - 5, p.y), ImVec2(p.x + 5, p.y), Col(Rgb(255, 244, 228), al * 0.35f), 1.f);
                d->AddLine(ImVec2(p.x, p.y - 5), ImVec2(p.x, p.y + 5), Col(Rgb(255, 244, 228), al * 0.35f), 1.f);
            }
        }
        // редкий метеор
        static float mtimer = 4.f, mlife = -1.f; static ImVec2 mp0;
        mtimer -= dt;
        if (mlife < 0.f && mtimer < 0.f) { mlife = 0.f; mp0 = ImVec2(wp.x + (0.25f + (rand() % 600) / 1000.f) * ws.x, wp.y + (0.05f + (rand() % 300) / 1000.f) * ws.y); mtimer = 8.f + (rand() % 70) / 10.f; }
        if (mlife >= 0.f) {
            mlife += dt; float k = mlife / 0.9f;
            if (k >= 1.f) mlife = -1.f;
            else {
                ImVec2 dir(0.84f, 0.54f); float dist = k * 520.f; float fade = sinf(k * 3.14159f);
                ImVec2 head(mp0.x + dir.x * dist, mp0.y + dir.y * dist);
                for (int i = 0; i < 12; ++i) {
                    float a0 = i / 12.f, a1 = (i + 1) / 12.f;
                    d->AddLine(ImVec2(head.x - dir.x * 110.f * a0, head.y - dir.y * 110.f * a0), ImVec2(head.x - dir.x * 110.f * a1, head.y - dir.y * 110.f * a1),
                        Col(Rgb(255, 236, 214), (1.f - a0) * 0.55f * fade * night), 1.6f);
                }
            }
        }
    }
    // виньетка
    {
        ImVec4 vc = LerpColor(Rgb(0, 0, 0), Rgb(70, 48, 24), g_ThemeAnim); float va = 0.50f - 0.38f * g_ThemeAnim;
        ImU32 v0 = Col(vc, va), v1 = Col(vc, 0.f); const float V = 170.f;
        d->AddRectFilledMultiColor(wp, ImVec2(wp.x + V, e.y), v0, v1, v1, v0);
        d->AddRectFilledMultiColor(ImVec2(e.x - V, wp.y), e, v1, v0, v0, v1);
        d->AddRectFilledMultiColor(wp, ImVec2(e.x, wp.y + V * 0.7f), v0, v0, v1, v1);
        d->AddRectFilledMultiColor(ImVec2(wp.x, e.y - V * 0.7f), e, v1, v1, v0, v0);
    }
    // плёночное зерно: прячет бандинг свечения и добавляет «осязаемость»
    {
        static std::vector<ImVec2> grain; static float gtm = 0.f;
        gtm -= dt;
        if (grain.empty() || gtm <= 0.f) { grain.resize(300); for (auto& g : grain) g = ImVec2((rand() % 10000) / 10000.f, (rand() % 10000) / 10000.f); gtm = 0.08f; }
        ImU32 gc = Col(LerpColor(WHITE, Rgb(0, 0, 0), g_ThemeAnim), 0.045f + 0.03f * g_ThemeAnim);
        for (auto& g : grain) { ImVec2 p(wp.x + g.x * ws.x, wp.y + g.y * ws.y); d->AddRectFilled(p, ImVec2(p.x + 1.3f, p.y + 1.3f), gc); }
    }
}

static void DrawNavIcon(ImDrawList* d, int i, ImVec2 c, ImU32 col) {
    const float TH = 1.7f;
    if (i == 0) {          // чёрная дыра: тень + наклонённое кольцо
        d->AddCircleFilled(c, 4.6f, col, 20);
        ImVec2 pts[28]; float ct = cosf(-0.35f), st = sinf(-0.35f);
        for (int k = 0; k < 28; ++k) { float a = k / 28.f * 6.28318f; float x = cosf(a) * 12.f, y = sinf(a) * 4.2f; pts[k] = ImVec2(c.x + x * ct - y * st, c.y + x * st + y * ct); }
        d->AddPolyline(pts, 28, col, ImDrawFlags_Closed, TH);
    }
    else if (i == 1) {     // три регулятора
        const float ys[3] = { -7.f, 0.f, 7.f }, ks[3] = { -4.f, 5.f, -1.f };
        for (int k = 0; k < 3; ++k) {
            d->AddLine(ImVec2(c.x - 11, c.y + ys[k]), ImVec2(c.x + 11, c.y + ys[k]), col, TH);
            d->AddCircleFilled(ImVec2(c.x + ks[k], c.y + ys[k]), 3.4f, Col(g_P.bg), 16);
            d->AddCircle(ImVec2(c.x + ks[k], c.y + ys[k]), 3.4f, col, 16, TH);
        }
    }
    else {                 // информация
        d->AddCircle(c, 11.5f, col, 28, TH);
        d->AddCircleFilled(ImVec2(c.x, c.y - 5), 1.5f, col, 10);
        d->AddLine(ImVec2(c.x, c.y - 1), ImVec2(c.x, c.y + 6), col, TH);
    }
}

static void DrawTitleBar(ImDrawList* d, ImVec2 wp, ImVec2 ws, float tm) {
    const float H = 44.f;
    d->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + H), Col(g_P.bg, 0.85f), Col(g_P.bg, 0.85f), Col(g_P.bg, 0.f), Col(g_P.bg, 0.f));
    d->AddLine(ImVec2(wp.x, wp.y + H), ImVec2(wp.x + ws.x, wp.y + H), Col(g_P.border), 1.f);
    // знак: затмение с орбитой
    ImVec2 lc(wp.x + 32.f, wp.y + 22.f);
    d->AddCircleFilled(lc, 7.5f, Col(ACCENT), 24);
    d->AddCircleFilled(ImVec2(lc.x + 2.6f, lc.y - 1.6f), 7.f, Col(g_P.bg), 24);
    float oa = tm * 1.1f;
    d->AddCircle(lc, 12.5f, Col(AccentT(), 0.35f), 32, 1.f);
    d->AddCircleFilled(ImVec2(lc.x + cosf(oa) * 12.5f, lc.y + sinf(oa) * 12.5f), 1.7f, Col(AccentT()), 10);
    float w1 = DLabel(d, ImVec2(wp.x + 58, wp.y + 14), Col(g_P.text), "QYPXR", 3.4f);
    DLabel(d, ImVec2(wp.x + 58 + w1 + 10, wp.y + 14), Col(g_P.dim), "METHOD", 3.4f);

    float bx = wp.x + ws.x - 132.f;
    for (int i = 0; i < 3; ++i) {
        ImVec2 p(bx + i * 44.f, wp.y);
        ImGui::SetCursorScreenPos(p); char id[8]; snprintf(id, sizeof(id), "##wb%d", i);
        bool clk = Fx(ImGui::InvisibleButton(id, ImVec2(44, 44))); bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGuiStorage* st = ImGui::GetStateStorage(); ImGuiID gid = ImGui::GetItemID();
        float k = st->GetFloat(gid, 0.f); k = Lerp(k, hov ? 1.f : 0.f, std::min(1.f, ImGui::GetIO().DeltaTime * 14.f)); st->SetFloat(gid, k);
        ImVec2 cc(p.x + 22.f, p.y + 22.f); bool isClose = (i == 2);
        ImVec4 hue = isClose ? Rgb(232, 62, 78) : ACCENT;
        if (k > 0.01f) {   // подложка + мягкое свечение
            for (int g = 1; g <= 6; ++g) d->AddRectFilled(ImVec2(cc.x - 14 - g, cc.y - 14 - g), ImVec2(cc.x + 14 + g, cc.y + 14 + g), Col(hue, 0.03f * k * (1.f - g / 7.f)), 9.f + g);
            d->AddRectFilled(ImVec2(cc.x - 14, cc.y - 14), ImVec2(cc.x + 14, cc.y + 14), Col(hue, (isClose ? 0.85f : 0.16f) * k), 8.f);
            d->AddRect(ImVec2(cc.x - 14, cc.y - 14), ImVec2(cc.x + 14, cc.y + 14), Col(hue, 0.55f * k), 8.f, 0, 1.f);
        }
        ImU32 ic = Col(LerpColor(g_P.text, (isClose ? WHITE : AccentT()), k), 0.78f + 0.22f * k);
        const float TH = 1.5f;
        if (i == 0) {          // свернуть: линия растёт и опускается
            float hw = 5.5f + 1.5f * k, yy = cc.y + 3.f * k - 1.5f * (1.f - k);
            d->AddLine(ImVec2(cc.x - hw, yy), ImVec2(cc.x + hw, yy), ic, TH + 0.3f);
        }
        else if (i == 1) {     // развернуть / восстановить
            if (IsZoomed(g_hwnd)) {
                d->AddRect(ImVec2(cc.x - 5.f, cc.y - 2.f), ImVec2(cc.x + 3.f, cc.y + 6.f), ic, 1.5f, 0, TH);
                d->AddLine(ImVec2(cc.x - 2.f, cc.y - 5.f), ImVec2(cc.x + 6.f, cc.y - 5.f), ic, TH); d->AddLine(ImVec2(cc.x + 6.f, cc.y - 5.f), ImVec2(cc.x + 6.f, cc.y + 3.f), ic, TH);
                d->AddLine(ImVec2(cc.x - 2.f, cc.y - 5.f), ImVec2(cc.x - 2.f, cc.y - 2.f), ic, TH);
            }
            else { float e = 5.5f + 1.2f * k; d->AddRect(ImVec2(cc.x - e, cc.y - e), ImVec2(cc.x + e, cc.y + e), ic, 2.f, 0, TH); }
        }
        else {                 // закрыть: крест поворачивается на 90°
            float ang = k * 1.5708f, s = 5.8f;
            for (int q = 0; q < 2; ++q) {
                float a = 0.7854f + q * 1.5708f + ang;
                d->AddLine(ImVec2(cc.x - cosf(a) * s, cc.y - sinf(a) * s), ImVec2(cc.x + cosf(a) * s, cc.y + sinf(a) * s), ic, TH + 0.1f);
            }
        }
        if (clk) { if (i == 0) ShowWindow(g_hwnd, SW_MINIMIZE); else if (i == 1) ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); else SendMessage(g_hwnd, WM_CLOSE, 0, 0); }
    }
}

static void DrawSidebar(ImDrawList* d, ImVec2 wp, ImVec2 ws, float dt) {
    const float TB = 44.f, SB = 84.f;
    ImVec2 a(wp.x, wp.y + TB), b(wp.x + SB, wp.y + ws.y);
    d->AddRectFilledMultiColor(a, b, Col(g_P.bg, 0.78f), Col(g_P.bg, 0.45f), Col(g_P.bg, 0.45f), Col(g_P.bg, 0.78f));
    d->AddLine(ImVec2(b.x, a.y), b, Col(g_P.border), 1.f);
    const char* tips[3] = { "КОНВЕРТЕР", "НАСТРОЙКИ", "ПОМОЩЬ" };
    for (int i = 0; i < 3; ++i) {
        ImVec2 p(wp.x + 14.f, a.y + 22.f + i * 68.f);
        ImGui::SetCursorScreenPos(p); char id[8]; snprintf(id, sizeof(id), "##nav%d", i);
        bool nclk = ImGui::InvisibleButton(id, ImVec2(56, 56)); Fx(nclk && g_Page != i);
        if (nclk && g_Page != i) { g_Page = i; g_PageAlpha = 0.f; }
        bool hov = ImGui::IsItemHovered(); if (hov) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        g_NavHover[i] = Lerp(g_NavHover[i], hov ? 1.f : 0.f, std::min(1.f, dt * 14.f));
        bool act = (g_Page == i);
        ImVec2 cc(p.x + 28, p.y + 28);
        if (act) {
            d->AddRectFilledMultiColor(ImVec2(wp.x, p.y), ImVec2(wp.x + SB - 1, p.y + 56), Col(ACCENT, 0.16f), Col(ACCENT, 0.f), Col(ACCENT, 0.f), Col(ACCENT, 0.16f));
            d->AddRectFilled(ImVec2(wp.x, p.y + 10), ImVec2(wp.x + 2.5f, p.y + 46), Col(ACCENT), 1.f);
            for (int k = 1; k <= 6; ++k) d->AddCircleFilled(cc, 14.f + k * 3.f, Col(ACCENT, 0.018f), 24);
        }
        else if (g_NavHover[i] > 0.01f) d->AddRectFilled(ImVec2(wp.x + 10, p.y + 4), ImVec2(wp.x + SB - 10, p.y + 52), Col(g_P.panel2, g_NavHover[i]), 4.f);
        DrawNavIcon(d, i, cc, act ? Col(AccentT()) : Col(g_P.dim, 0.75f + 0.25f * g_NavHover[i]));
        if (g_NavHover[i] > 0.02f) {   // свой tooltip: выезжает справа от иконки
            ImDrawList* fd = ImGui::GetForegroundDrawList(); float k = Smooth(g_NavHover[i]);
            std::string t = tips[i]; ImVec2 ts(TrackWidth(g_FontMono, t.c_str(), 1.6f), 14.f);
            ImVec2 ta(wp.x + SB + 6.f + (1.f - k) * -8.f, p.y + 28.f - 15.f), tb(ta.x + ts.x + 26.f, ta.y + 30.f);
            float sv = g_A; g_A = k;
            fd->AddRectFilled(ta, tb, Col(g_P.solid), 4.f); fd->AddRect(ta, tb, Col(g_P.border, 1.8f), 4.f);
            fd->AddRectFilled(ImVec2(ta.x, ta.y + 7), ImVec2(ta.x + 2.5f, tb.y - 7), Col(ACCENT), 1.f);
            std::string up = t; DLabel(fd, ImVec2(ta.x + 14, ta.y + 8), Col(g_P.text), up.c_str(), 1.6f);
            g_A = sv;
        }
    }
    DLabelC(d, wp.x + SB * 0.5f, b.y - 32.f, Col(g_P.dim, 0.7f), "V2.0", 1.5f);
}

static void DrawToasts(float dt) {
    ImDrawList* d = ImGui::GetForegroundDrawList(); ImGuiViewport* vp = ImGui::GetMainViewport();
    std::lock_guard<std::mutex> lock(g_UIMutex);
    if (!g_ToastsOn) { g_Toasts.clear(); return; }
    float y = vp->WorkPos.y + vp->WorkSize.y - 24.f; float rx = vp->WorkPos.x + vp->WorkSize.x - 24.f;
    g_A = 1.f;
    for (auto it = g_Toasts.begin(); it != g_Toasts.end(); ) {
        float age = 3.f - it->timer; if (age < 0) age = 0;
        float in = Smooth(std::min(1.f, age / 0.25f)), out = std::min(1.f, it->timer / 0.4f), al = in * out;
        ImVec2 ts = TSize(g_FontBody, it->text.c_str());
        float w = ts.x + 60.f, h = 46.f; float slide = (1.f - in) * 40.f;
        ImVec2 a(rx - w + slide, y - h), b(rx + slide, y);
        for (int i = 1; i <= 8; ++i) d->AddRectFilled(ImVec2(a.x - i, a.y - i + 3), ImVec2(b.x + i, b.y + i + 3), Col(ImVec4(0, 0, 0, 0.05f), al * (1.f - i / 9.f)), 4.f + i);
        d->AddRectFilled(a, b, Col(g_P.solid, al), 4.f);
        d->AddRect(a, b, Col(g_P.border, 1.6f * al), 4.f, 0, 1.f);
        d->AddRectFilled(a, ImVec2(a.x + 3.f, b.y), Col(ACCENT, al), 2.f);
        float prog = std::clamp(it->timer / 3.f, 0.f, 1.f);
        d->AddRectFilled(ImVec2(a.x + 3.f, b.y - 2.f), ImVec2(a.x + 3.f + (w - 3.f) * prog, b.y), Col(ACCENT, 0.7f * al), 0.f);
        ImGui::PushFont(g_FontBody); d->AddText(ImVec2(a.x + 22, a.y + (h - ts.y) * 0.5f - 1.f), Col(g_P.text, al), it->text.c_str()); ImGui::PopFont();
        y -= h + 10.f;
        it->timer -= dt;
        if (it->timer <= 0.f) it = g_Toasts.erase(it); else ++it;
    }
}
// ============================================================================
//  Заставка запуска (фейковая загрузка)
// ============================================================================
static float g_SplashT = 0.f;
static const float SPLASH_LEN = 3.3f, SPLASH_FADE = 0.7f;
static bool SplashActive() { return g_SplashT < SPLASH_LEN + SPLASH_FADE; }
static void DrawSplash(ImDrawList* d, ImVec2 wp, ImVec2 ws, float dt) {
    if (!SplashActive()) return;
    g_SplashT += dt;
    ImGuiIO& io = ImGui::GetIO();
    if (g_SplashT < SPLASH_LEN - 0.2f && g_SplashT > 0.5f && (ImGui::IsMouseClicked(0) || ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter))) g_SplashT = SPLASH_LEN - 0.2f;
    float t = g_SplashT, fade = t > SPLASH_LEN ? 1.f - Smooth(std::min(1.f, (t - SPLASH_LEN) / SPLASH_FADE)) : 1.f;
    float zoom = 1.f + (1.f - fade) * 0.9f;                           // при исчезновении камера «влетает» в дыру
    float prog = Smooth(std::min(1.f, t / (SPLASH_LEN - 0.35f)));
    float fadeIn = Smooth(std::min(1.f, t / 0.6f));
    float savedA = g_A; g_A = fade;
    DrawBackground(d, wp, ws, t, dt);
    g_A = fade * fadeIn;
    ImVec2 ctr(wp.x + ws.x * 0.5f, wp.y + ws.y * 0.5f - 62.f);
    float R = 58.f * zoom;
    d->PushClipRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), true);
    DrawBlackHole(d, ctr, R, t * 1.2f, 0.25f + 0.75f * prog);
    d->PopClipRect();
    float ty = ctr.y + 150.f;
    const char* name = "QYPXR"; float nw = TrackWidth(g_FontBig, name, 16.f);
    DTrack(d, g_FontBig, ImVec2(ctr.x - nw * 0.5f, ty), Col(g_P.text), name, 16.f);
    const char* sub = "M E T H O D"; DLabelC(d, ctr.x, ty + 66.f, Col(AccentT()), sub, 5.f);
    // шкала
    float bw = 340.f, bx = ctr.x - bw * 0.5f, by = ty + 112.f;
    const char* stage = prog < 0.25f ? "ИНИЦИАЛИЗАЦИЯ ЯДРА" : prog < 0.50f ? "ЗАГРУЗКА ШРИФТОВ" : prog < 0.75f ? "СИНТЕЗ ЗВУКОВ" : prog < 0.97f ? "ПОДГОТОВКА ИНТЕРФЕЙСА" : "ГОТОВО";
    DLabelC(d, ctr.x, by - 28.f, Col(g_P.dim), stage, 2.4f);
    d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + 2.f), Col(g_P.text, 0.14f), 1.f);
    for (int i = 0; i <= 20; ++i) { float x = bx + bw * i / 20.f; d->AddLine(ImVec2(x, by + 6.f), ImVec2(x, by + (i % 5 == 0 ? 12.f : 9.f)), Col(g_P.dim, i / 20.f <= prog ? 0.7f : 0.25f), 1.f); }
    d->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw * prog, by + 2.f), Col(ACCENT), 1.f);
    for (int i = 1; i <= 8; ++i) d->AddCircleFilled(ImVec2(bx + bw * prog, by + 1.f), 2.f + i * 2.4f, Col(ACCENT, 0.05f), 20);
    char pc[16]; snprintf(pc, sizeof(pc), "%d%%", (int)(prog * 100.f + 0.5f)); DLabelC(d, ctr.x, by + 22.f, Col(AccentT()), pc, 2.0f);
    if (t > 0.8f && t < SPLASH_LEN - 0.2f) DLabelC(d, ctr.x, wp.y + ws.y - 46.f, Col(g_P.dim, 0.55f + 0.2f * sinf(t * 3.f)), "НАЖМИТЕ, ЧТОБЫ ПРОПУСТИТЬ", 2.0f);
    g_A = savedA; (void)io;
}
// ============================================================================
//  main
// ============================================================================
int APIENTRY wWinMain(_In_ HINSTANCE hInstance, _In_opt_ HINSTANCE hPrevInstance, _In_ LPWSTR lpCmdLine, _In_ int nCmdShow)
{
    SetCurrentProcessExplicitAppUserModelID(L"QYPXR.METHOD.2");   // своя группа и иконка на панели задач
    LoadSettings(); srand((unsigned int)time(0)); g_CurrentQuoteIdx = rand() % (int)g_Quotes.size();
    HICON hIconBig = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInstance, hIconBig, nullptr, nullptr, nullptr, L"QYPXR_Engine", hIconSm };
    ::RegisterClassExW(&wc); DWORD dwStyle = WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    HWND hwnd = ::CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"QYPXR METHOD", dwStyle, 100, 100, 1040, 780, nullptr, nullptr, wc.hInstance, nullptr);
    g_hwnd = hwnd;
    if (hIconBig) SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
    if (hIconSm) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSm);
    MARGINS margins = { 1, 1, 1, 1 }; DwmExtendFrameIntoClientArea(hwnd, &margins); DragAcceptFiles(hwnd, TRUE);
    if (!CreateDeviceD3D(hwnd)) { CleanupDeviceD3D(); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 1; }

    ::ShowWindow(hwnd, SW_SHOWDEFAULT); ::UpdateWindow(hwnd);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.IniFilename = nullptr;
    BuildSounds();

    // шрифты: кириллица + типографские символы (→ — … • « » ·)
    static ImVector<ImWchar> ranges; ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic()); builder.AddText("→—…•«»·×");
    builder.BuildRanges(&ranges);
    ImFontConfig fc; fc.OversampleH = 2; fc.OversampleV = 2; fc.GlyphRanges = ranges.Data;
    auto LoadF = [&](const char* path, float size, ImFont* fb) -> ImFont* {
        std::error_code ec; if (!std::filesystem::exists(path, ec)) return fb;
        ImFont* f = io.Fonts->AddFontFromFileTTF(path, size, &fc, ranges.Data); return f ? f : fb;
        };
    g_FontBody = LoadF("C:\\Windows\\Fonts\\segoeui.ttf", 18.0f, nullptr);
    if (!g_FontBody) { g_FontBody = io.Fonts->AddFontDefault(); }
    g_FontSmall = LoadF("C:\\Windows\\Fonts\\segoeui.ttf", 15.0f, g_FontBody);
    g_FontH2 = LoadF("C:\\Windows\\Fonts\\segoeui.ttf", 22.0f, g_FontBody);
    g_FontH1 = LoadF("C:\\Windows\\Fonts\\segoeuil.ttf", 36.0f, g_FontH2);   // Segoe UI Light
    g_FontBig = LoadF("C:\\Windows\\Fonts\\segoeuil.ttf", 50.0f, g_FontH1);
    g_FontMono = LoadF("C:\\Windows\\Fonts\\consola.ttf", 14.0f, g_FontSmall);  // Consolas

    ImGui::StyleColorsDark(); ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f; style.WindowPadding = ImVec2(0.0f, 0.0f); style.WindowBorderSize = 0.0f;
    ImGui_ImplWin32_Init(hwnd); ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) { ::TranslateMessage(&msg); ::DispatchMessage(&msg); if (msg.message == WM_QUIT) done = true; }
        if (done) break;

        float dt = io.DeltaTime; if (dt <= 0.f || dt > 0.1f) dt = 1.f / 60.f;
        float sp = dt * 2.5f;
        if (g_IsLightTheme && g_ThemeAnim < 1.f) g_ThemeAnim += sp;
        if (!g_IsLightTheme && g_ThemeAnim > 0.f) g_ThemeAnim -= sp;
        g_ThemeAnim = std::clamp(g_ThemeAnim, 0.f, 1.f);
        g_PageAlpha = std::min(1.f, g_PageAlpha + dt * 6.f);
        UpdatePalette();
        { float k = std::min(1.f, dt * 8.f); ACCENT = LerpColor(ACCENT, g_Accents[g_AccentIdx].a, k); ACCENT_HI = LerpColor(ACCENT_HI, g_Accents[g_AccentIdx].hi, k); }

        style.Colors[ImGuiCol_WindowBg] = g_P.bg; style.Colors[ImGuiCol_Text] = g_P.text;
        style.Colors[ImGuiCol_PopupBg] = g_P.panel2; style.Colors[ImGuiCol_Border] = g_P.border;

        if (!g_DroppedFilePath.empty()) { g_SelectedFilePath = g_DroppedFilePath; g_DroppedFilePath.clear(); AddToast("Файл успешно загружен"); g_Page = 0; Snd(SFX_PICK); }
        { int ps = g_PendingSnd.exchange(-1); if (ps >= 0) { Snd(ps); if (ps == SFX_OK && ::GetForegroundWindow() != g_hwnd) { FLASHWINFO fi = { sizeof(fi), g_hwnd, FLASHW_ALL | FLASHW_TIMERNOFG, 3, 0 }; ::FlashWindowEx(&fi); } } }
        { static bool started = false; if (!started) { started = true; Snd(SFX_START); } }

        ImGui_ImplDX11_NewFrame(); ImGui_ImplWin32_NewFrame(); ImGui::NewFrame();
        ImGuiViewport* vp = ImGui::GetMainViewport(); ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
        ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;
        ImGui::Begin("MainWorkspace", nullptr, wf);

        ImDrawList* d = ImGui::GetWindowDrawList();
        ImVec2 wp = vp->WorkPos, ws = vp->WorkSize; float time = (float)ImGui::GetTime();
        g_A = 1.f;
        DrawBackground(d, wp, ws, time, dt);

        // контент
        const float TB = 44.f, SB = 84.f;
        Ctx cx{ d, ImVec2(wp.x + SB, wp.y + TB), ImVec2(ws.x - SB, ws.y - TB), time, dt };
        g_A = Smooth(g_PageAlpha);
        const bool dis = g_PickerOpen || SplashActive();   // пока открыт выбор файла — страницы и сайдбар неактивны
        const ImVec2 savedMouse = io.MousePos; if (dis) io.MousePos = ImVec2(-FLT_MAX, -FLT_MAX);   // страница не должна «видеть» мышь
        d->PushClipRect(cx.o, ImVec2(cx.o.x + cx.sz.x, cx.o.y + cx.sz.y), true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, std::max(g_A, 0.02f));
        if (g_Page == 0) PageConvert(cx); else if (g_Page == 1) PageSettings(cx); else PageHelp(cx);
        ImGui::PopStyleVar();
        d->PopClipRect();
        g_A = 1.f;

        DrawSidebar(d, wp, ws, dt);
        if (dis) io.MousePos = savedMouse;
        DrawFilePicker(d, wp, ws, dt);
        DrawSplash(d, wp, ws, dt);
        DrawTitleBar(d, wp, ws, time);
        d->AddRect(wp, ImVec2(wp.x + ws.x, wp.y + ws.y), Col(g_P.border), 0.f, 0, 1.f);

        ImGui::End();
        DrawToasts(dt);

        ImGui::Render();
        ImVec4 c_bg = g_P.bg; const float clr[4] = { c_bg.x, c_bg.y, c_bg.z, 1.f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr); g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData()); g_pSwapChain->Present(1, 0);
    }

    SaveSettings();
    ImGui_ImplDX11_Shutdown(); ImGui_ImplWin32_Shutdown(); ImGui::DestroyContext(); CleanupDeviceD3D(); ::DestroyWindow(hwnd); ::UnregisterClassW(wc.lpszClassName, wc.hInstance); return 0;
}

// ============================================================================
//  D3D / WinAPI (без изменений по логике)
// ============================================================================
bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd; ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2; sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; sd.BufferDesc.RefreshRate.Numerator = 60; sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1; sd.Windowed = TRUE; sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    D3D_FEATURE_LEVEL fL; const D3D_FEATURE_LEVEL fLA[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, fLA, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &fL, &g_pd3dDeviceContext) != S_OK) return false;
    CreateRenderTarget(); return true;
}
void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}
void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) { g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView); pBackBuffer->Release(); }
}
void CleanupRenderTarget() { if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; } }

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg) {
    case WM_DROPFILES: {
        if (g_IsProcessing) break;
        HDROP hDrop = (HDROP)wParam; wchar_t filePath[MAX_PATH];
        if (DragQueryFileW(hDrop, 0, filePath, MAX_PATH)) { g_DroppedFilePath = WStringToString(filePath); }
        DragFinish(hDrop); return 0;
    }
    case WM_GETMINMAXINFO: { MINMAXINFO* mm = (MINMAXINFO*)lParam; mm->ptMinTrackSize.x = 940; mm->ptMinTrackSize.y = 720; return 0; }
    case WM_NCCALCSIZE: if (wParam == TRUE) return 0; break;
    case WM_NCHITTEST: {
        LRESULT hit = ::DefWindowProcW(hWnd, msg, wParam, lParam);
        if (hit == HTNOWHERE || hit == HTCLIENT) {
            POINT pt; pt.x = (int)(short)LOWORD(lParam); pt.y = (int)(short)HIWORD(lParam);
            RECT rc; GetWindowRect(hWnd, &rc);
            int x = pt.x - rc.left, y = pt.y - rc.top, width = rc.right - rc.left, height = rc.bottom - rc.top;
            int border = 8, title_height = 44, buttons_width = 132;
            if (!IsZoomed(hWnd)) {
                if (x < border && y < border) return HTTOPLEFT;
                if (x > width - border && y < border) return HTTOPRIGHT;
                if (x < border && y > height - border) return HTBOTTOMLEFT;
                if (x > width - border && y > height - border) return HTBOTTOMRIGHT;
                if (x < border) return HTLEFT; if (x > width - border) return HTRIGHT;
                if (y < border) return HTTOP; if (y > height - border) return HTBOTTOM;
            }
            if (y < title_height && x < width - buttons_width) return HTCAPTION;
        }
        return hit;
    }
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED) return 0;
        if (g_pd3dDevice != nullptr) { CleanupRenderTarget(); g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0); CreateRenderTarget(); }
        return 0;
    case WM_SYSCOMMAND: if ((wParam & 0xfff0) == SC_KEYMENU) return 0; break;
    case WM_DESTROY: ::PostQuitMessage(0); return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}