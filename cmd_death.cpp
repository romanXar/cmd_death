#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX
#include <windows.h>
#include <gdiplus.h>
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <conio.h>
#include <ctime>
#include <thread>
#include <atomic>

#pragma comment(lib, "gdiplus.lib")
using namespace Gdiplus;

static float SMOOTH_MIX = 0.35f;

static CONSOLE_FONT_INFOEX g_origFont = {};
static COORD                g_origBufSize = {};
static HWND                 g_hwnd = nullptr;
static bool                 g_saved = false;

static void RestoreConsole() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_saved) {
        if (g_hwnd) ShowWindow(g_hwnd, SW_RESTORE);
        SetCurrentConsoleFontEx(h, FALSE, &g_origFont);
        SetConsoleScreenBufferSize(h, g_origBufSize);
    }
}
static BOOL WINAPI CtrlHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_CLOSE_EVENT || type == CTRL_BREAK_EVENT)
        RestoreConsole();
    return FALSE;
}
static void SaveConsoleState() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hwnd = GetConsoleWindow();
    g_origFont.cbSize = sizeof(g_origFont);
    GetCurrentConsoleFontEx(h, FALSE, &g_origFont);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    g_origBufSize = csbi.dwSize;
    g_saved = true;
}
static void MaximizeConsoleAndShrinkFont(int fontSizeY, const wchar_t* faceName) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    HWND hwnd = GetConsoleWindow();
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    SHORT cols = (SHORT)(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    SHORT rows = (SHORT)(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    COORD newBuf = { cols, (SHORT)(rows + 200) };
    SetConsoleScreenBufferSize(h, newBuf);
    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(cfi);
    GetCurrentConsoleFontEx(h, FALSE, &cfi);
    cfi.dwFontSize.X = 0;
    cfi.dwFontSize.Y = (SHORT)fontSizeY;
    wcsncpy_s(cfi.FaceName, faceName, _TRUNCATE);
    SetCurrentConsoleFontEx(h, FALSE, &cfi);
    if (hwnd) ShowWindow(hwnd, SW_MAXIMIZE);
}

static inline int toneCurve(int c, float A) {
    if (A < 1.0f) A = 1.0f; if (A > 1.5f) A = 1.5f;
    float x = c / 255.0f;
    float d = x - 0.5f;
    float B = 4.0f * (1.0f - A);
    float y = 0.5f + d * (A + B * d * d);
    const float LIFT = 8.0f / 255.0f;
    y = LIFT + (1.0f - LIFT) * y;
    if (y < 0) y = 0; if (y > 1) y = 1;
    return (int)(y * 255.0f + 0.5f);
}
static inline void rgb2hsv(int r, int g, int b, float& h, float& s, float& v) {
    float R = r / 255.0f, G = g / 255.0f, B = b / 255.0f;
    float mx = fmaxf(R, fmaxf(G, B)), mn = fminf(R, fminf(G, B));
    float d = mx - mn;
    v = mx; s = (mx <= 0) ? 0 : (d / mx);
    if (d < 1e-6f) h = 0;
    else if (mx == R) h = 60.0f * fmodf((G - B) / d + 6.0f, 6.0f);
    else if (mx == G) h = 60.0f * ((B - R) / d + 2.0f);
    else              h = 60.0f * ((R - G) / d + 4.0f);
}
static inline void hsv2rgb(float h, float s, float v, int& r, int& g, int& b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float R, G, B;
    if (h < 60) { R = c;G = x;B = 0; }
    else if (h < 120) { R = x;G = c;B = 0; }
    else if (h < 180) { R = 0;G = c;B = x; }
    else if (h < 240) { R = 0;G = x;B = c; }
    else if (h < 300) { R = x;G = 0;B = c; }
    else { R = c;G = 0;B = x; }
    r = (int)((R + m) * 255 + 0.5f);
    g = (int)((G + m) * 255 + 0.5f);
    b = (int)((B + m) * 255 + 0.5f);
    if (r < 0)r = 0; if (r > 255)r = 255;
    if (g < 0)g = 0; if (g > 255)g = 255;
    if (b < 0)b = 0; if (b > 255)b = 255;
}
static inline void boostBrightSaturation(int& r, int& g, int& b, float mb, float th) {
    float h, s, v; rgb2hsv(r, g, b, h, s, v);
    if (v <= th || s < 1e-6f) return;
    float t = (v - th) / (1.0f - th); if (t > 1) t = 1;
    s = s + mb * t * t * (1.0f - s * 0.35f);
    if (s > 1) s = 1;
    hsv2rgb(h, s, v, r, g, b);
}

static inline void firePalette(float u, int& r, int& g, int& b) {
    if (u < 0) u = 0; if (u > 1) u = 1;
    static const float T[6] = { 0.00f, 0.15f, 0.35f, 0.55f, 0.75f, 1.00f };
    static const int   R[6] = { 0,    60,   180,   255,   255,   255 };
    static const int   G[6] = { 0,     0,    20,    90,   190,   250 };
    static const int   B[6] = { 0,     0,     0,     0,    40,   200 };
    for (int i = 0; i < 5; ++i) {
        if (u <= T[i + 1]) {
            float t = (u - T[i]) / (T[i + 1] - T[i]);
            r = (int)(R[i] + t * (R[i + 1] - R[i]));
            g = (int)(G[i] + t * (G[i + 1] - G[i]));
            b = (int)(B[i] + t * (B[i + 1] - B[i]));
            return;
        }
    }
    r = R[5]; g = G[5]; b = B[5];
}
static inline void blackFirePalette(float u, int& r, int& g, int& b) {
    if (u < 0) u = 0; if (u > 1) u = 1;
    static const float T[5] = { 0.00f, 0.30f, 0.55f, 0.75f, 1.00f };
    static const int   R[5] = { 0,    20,    60,   140,   240 };
    static const int   G[5] = { 0,    10,    30,   130,   230 };
    static const int   B[5] = { 20,   100,   200,   240,   255 };
    for (int i = 0; i < 4; ++i) {
        if (u <= T[i + 1]) {
            float t = (u - T[i]) / (T[i + 1] - T[i]);
            r = (int)(R[i] + t * (R[i + 1] - R[i]));
            g = (int)(G[i] + t * (G[i + 1] - G[i]));
            b = (int)(B[i] + t * (B[i + 1] - B[i]));
            return;
        }
    }
    r = R[4]; g = G[4]; b = B[4];
}

static inline float smoothstep(float t) { return t * t * (3.0f - 2.0f * t); }

static void regenerateJitter(std::vector<int>& grid, std::vector<float>& blendGrid, int gw, int gh, int amp) {
    std::vector<float> acc((size_t)gw * gh, 0.0f);
    const int blocks[4] = { 1, 2, 4, 8 };
    const int weights[4] = { 2, 3, 5, 8 };
    const int totalW = 18;

    for (int li = 0; li < 4; ++li) {
        int b = blocks[li];
        int w = weights[li];
        int ox = rand() % (b * 2);
        int oy = rand() % (b * 2);
        int lw = gw / b + 4;
        int lh = gh / b + 4;
        std::vector<float> lat((size_t)lw * lh);
        for (auto& v : lat) v = (float)((rand() % (2 * amp + 1)) - amp) * w / totalW;

        for (int gy = 0; gy < gh; ++gy) {
            for (int gx = 0; gx < gw; ++gx) {
                float fx = (gx + ox) / (float)b;
                float fy = (gy + oy) / (float)b;
                int x0 = (int)fx, y0 = (int)fy;
                int x1 = x0 + 1, y1 = y0 + 1;
                if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
                if (x1 >= lw) x1 = lw - 1; if (y1 >= lh) y1 = lh - 1;
                if (x0 >= lw) x0 = lw - 1; if (y0 >= lh) y0 = lh - 1;
                float tx = smoothstep(fx - x0);
                float ty = smoothstep(fy - y0);
                float v00 = lat[(size_t)y0 * lw + x0];
                float v10 = lat[(size_t)y0 * lw + x1];
                float v01 = lat[(size_t)y1 * lw + x0];
                float v11 = lat[(size_t)y1 * lw + x1];
                float v0 = v00 + (v10 - v00) * tx;
                float v1 = v01 + (v11 - v01) * tx;
                acc[(size_t)gy * gw + gx] += v0 + (v1 - v0) * ty;
            }
        }
    }
    grid.resize((size_t)gw * gh);
    for (size_t i = 0; i < acc.size(); ++i) {
        float v = acc[i];
        if (v > 127) v = 127;
        if (v < -127) v = -127;
        grid[i] = (int)v;
    }
    blendGrid.resize((size_t)gw * gh);
    float scale = (float)amp / 85.0f;
    float lowThresh = 5.0f * scale;
    float highThresh = 18.0f * scale;

    for (int y = 0; y < gh; ++y) {
        for (int x = 0; x < gw; ++x) {
            float sum = 0.0f;
            int   count = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int nx = x + dx, ny = y + dy;
                    if (nx >= 0 && nx < gw && ny >= 0 && ny < gh) {
                        sum += (float)grid[(size_t)ny * gw + nx];
                        count++;
                    }
                }
            }
            float centerVal = (float)grid[(size_t)y * gw + x];
            float avg = (count > 0) ? sum / count : centerVal;
            float diff = fabsf(centerVal - avg);
            float t = (diff - lowThresh) / (highThresh - lowThresh);
            if (t < 0.0f) t = 0.0f;
            if (t > 1.0f) t = 1.0f;
            blendGrid[(size_t)y * gw + x] = 1.0f - t;
        }
    }
}

static inline void sampleBilinear(const std::vector<uint32_t>& pix, int W, int H, float x, float y, int& r, int& g, int& b) {
    int x0 = (int)x; int y0 = (int)y;
    float fx = x - x0; float fy = y - y0;
    int x1 = x0 + 1, y1 = y0 + 1;

    if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0;
    if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
    if (x0 >= W) x0 = W - 1; if (y0 >= H) y0 = H - 1;
    if (x1 >= W) x1 = W - 1; if (y1 >= H) y1 = H - 1;

    uint32_t p00 = pix[(size_t)y0 * W + x0]; uint32_t p10 = pix[(size_t)y0 * W + x1];
    uint32_t p01 = pix[(size_t)y1 * W + x0]; uint32_t p11 = pix[(size_t)y1 * W + x1];

    int r00 = (p00 >> 16) & 0xFF, g00 = (p00 >> 8) & 0xFF, b00 = p00 & 0xFF;
    int r10 = (p10 >> 16) & 0xFF, g10 = (p10 >> 8) & 0xFF, b10 = p10 & 0xFF;
    int r01 = (p01 >> 16) & 0xFF, g01 = (p01 >> 8) & 0xFF, b01 = p01 & 0xFF;
    int r11 = (p11 >> 16) & 0xFF, g11 = (p11 >> 8) & 0xFF, b11 = p11 & 0xFF;

    float rA = r00 + (r10 - r00) * fx; float rB = r01 + (r11 - r01) * fx;
    float gA = g00 + (g10 - g00) * fx; float gB = g01 + (g11 - g01) * fx;
    float bA = b00 + (b10 - b00) * fx; float bB = b01 + (b11 - b01) * fx;

    r = (int)(rA + (rB - rA) * fy + 0.5f);
    g = (int)(gA + (gB - gA) * fy + 0.5f);
    b = (int)(bA + (bB - bA) * fy + 0.5f);
}

static inline void sampleCrisp(const std::vector<uint32_t>& pix, int W, int H, float x, float y, float smooth, int& r, int& g, int& b) {
    int nx = (int)(x + 0.5f);
    int ny = (int)(y + 0.5f);
    if (nx < 0) nx = 0; if (nx >= W) nx = W - 1;
    if (ny < 0) ny = 0; if (ny >= H) ny = H - 1;
    uint32_t pn = pix[(size_t)ny * W + nx];
    int nr = (pn >> 16) & 0xFF, ng = (pn >> 8) & 0xFF, nb = pn & 0xFF;

    if (smooth <= 0.001f) { r = nr; g = ng; b = nb; return; }

    int br, bg, bb;
    sampleBilinear(pix, W, H, x, y, br, bg, bb);
    r = (int)(nr * (1.0f - smooth) + br * smooth + 0.5f);
    g = (int)(ng * (1.0f - smooth) + bg * smooth + 0.5f);
    b = (int)(nb * (1.0f - smooth) + bb * smooth + 0.5f);
}

static std::vector<uint32_t> loadPixels(Bitmap* bmp, int newW, int newH) {
    Bitmap* resized = new Bitmap(newW, newH, PixelFormat32bppARGB);
    {
        Graphics g(resized);
        g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        g.DrawImage(bmp, 0, 0, newW, newH);
    }
    std::vector<uint32_t> pix((size_t)newW * newH);
    for (int y = 0; y < newH; ++y) {
        for (int x = 0; x < newW; ++x) {
            Color c; resized->GetPixel(x, y, &c);
            int r = toneCurve(c.GetR(), 1.4f);
            int g = toneCurve(c.GetG(), 1.4f);
            int b = toneCurve(c.GetB(), 1.4f);
            boostBrightSaturation(r, g, b, 0.45f, 0.55f);
            pix[(size_t)y * newW + x] = ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
        }
    }
    delete resized;
    return pix;
}

inline void fast_append_int(std::string& out, int val) {
    if (val < 10) { out += (char)('0' + val); return; }
    char buf[12]; int i = 0;
    while (val > 0) { buf[i++] = (char)('0' + (val % 10)); val /= 10; }
    while (i > 0) { out += buf[--i]; }
}

int wmain(int argc, wchar_t* argv[]) {
    if (argc < 2) return 1;
    bool twerk = false, sway = false;
    int  fireMode = 0;
    for (int i = 2; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--twerk"))     twerk = true;
        else if (!wcscmp(argv[i], L"--sway"))      sway = true;
        else if (!wcscmp(argv[i], L"--fire"))      fireMode = 1;
        else if (!wcscmp(argv[i], L"--blackfire")) fireMode = 3;
        else if (!wcscmp(argv[i], L"--smooth") && i + 1 < argc) {
            float v = (float)_wtof(argv[++i]);
            SMOOTH_MIX = (v < 0.0f) ? 0.0f : (v > 1.0f ? 1.0f : v);
        }
    }

    srand((unsigned)time(NULL));
    SetConsoleCtrlHandler(CtrlHandler, TRUE);
    atexit(RestoreConsole);

    GdiplusStartupInput gsi; ULONG_PTR token;
    if (GdiplusStartup(&token, &gsi, NULL) != Ok) return 1;

    Bitmap* bmp = new Bitmap(argv[1]);
    if (bmp->GetLastStatus() != Ok) { delete bmp; GdiplusShutdown(token); return 1; }

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(h, &mode);
    SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    SaveConsoleState();
    MaximizeConsoleAndShrinkFont(8, L"Consolas");
    Sleep(120);

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(h, &csbi);
    int consoleW = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    int consoleH = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    int availRows = consoleH - 2;

    int imgW = (int)bmp->GetWidth();
    int imgH = (int)bmp->GetHeight();
    double s = std::min((double)consoleW / imgW, (double)(availRows * 2) / imgH);

    int newW = (int)(imgW * s);
    int newH = (int)(imgH * s);
    if (newH % 2) newH--;
    int cellsW = (newW < 1) ? 1 : newW;
    int cellsH = (newH < 2) ? 1 : newH / 2;
    int offsetX = (consoleW - cellsW) / 2;
    int offsetY = (availRows - cellsH) / 2;

    std::vector<uint32_t> pixels = loadPixels(bmp, newW, newH);
    delete bmp;

    std::vector<int>   jitterGrid((size_t)cellsW * cellsH, 0);
    std::vector<float> blendGrid((size_t)cellsW * cellsH, 1.0f);
    const int JITTER_AMP = 85;
    const int JITTER_HOLD = 2;
    int lastJitterTick = -1000;

    regenerateJitter(jitterGrid, blendGrid, cellsW, cellsH, JITTER_AMP);

    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4;

    auto render = [&](float t) {
        int curTick = (int)(t * 30.0f);
        if (curTick / JITTER_HOLD != lastJitterTick / JITTER_HOLD) {
            regenerateJitter(jitterGrid, blendGrid, cellsW, cellsH, JITTER_AMP);
            lastJitterTick = curTick;
        }

        std::vector<std::string> thread_outputs(num_threads);
        std::vector<std::thread> workers;

        // Многопоточная генерация
        for (unsigned int tid = 0; tid < num_threads; ++tid) {
            workers.emplace_back([&, tid]() {
                std::string& out = thread_outputs[tid];
                out.reserve((size_t)(consoleW * (availRows + 2) * 24) / num_threads);

                int start_cy = (tid * availRows) / num_threads;
                int end_cy = ((tid + 1) * availRows) / num_threads;

                float camX = sway ? 3.0f * sinf(t * 1.1f) : 0.0f;
                float camY = sway ? 0.6f * sinf(t * 2.2f + 1.3f) : 0.0f;

                for (int cy = start_cy; cy < end_cy; ++cy) {
                    int py = cy - offsetY;
                    if (py < 0 || py >= cellsH) { out += "\r\n"; continue; }
                    for (int i = 0; i < offsetX; ++i) out += ' ';

                    float twerkX = twerk ? 1.8f * sinf(cy * 0.15f + t * 2.0f) : 0.0f;
                    float twerkY = twerk ? 0.5f * sinf(t * 2.0f) : 0.0f;
                    float totalShiftX = camX + twerkX;
                    float totalShiftY = camY + twerkY;

                    for (int cx = 0; cx < cellsW; ++cx) {
                        int tr, tg, tb, br, bg, bb;
                        float cellBlend = blendGrid[(size_t)py * cellsW + cx];
                        float actualSmooth = SMOOTH_MIX * cellBlend;

                        sampleCrisp(pixels, newW, newH, cx + totalShiftX, py * 2.0f + totalShiftY, actualSmooth, tr, tg, tb);
                        sampleCrisp(pixels, newW, newH, cx + totalShiftX, py * 2.0f + 1.0f + totalShiftY, actualSmooth, br, bg, bb);

                        int j = jitterGrid[(size_t)py * cellsW + cx];
                        float u = (j + 127.0f) / 254.0f;
                        float k = fabsf((float)j) / 127.0f;

                        if (fireMode != 0) {
                            int fr, fg, fb;
                            if (fireMode == 1) firePalette(u, fr, fg, fb);
                            else               blackFirePalette(u, fr, fg, fb);
                            tr = (int)(tr * (1.0f - k * 0.35f) + fr * (k * 0.85f));
                            tg = (int)(tg * (1.0f - k * 0.35f) + fg * (k * 0.85f));
                            tb = (int)(tb * (1.0f - k * 0.35f) + fb * (k * 0.85f));
                            br = (int)(br * (1.0f - k * 0.35f) + fr * (k * 0.85f));
                            bg = (int)(bg * (1.0f - k * 0.35f) + fg * (k * 0.85f));
                            bb = (int)(bb * (1.0f - k * 0.35f) + fb * (k * 0.85f));
                        }
                        else {
                            tr += j; tg += j; tb += j; br += j; bg += j; bb += j;
                        }

                        if (tr < 0) tr = 0; if (tr > 255) tr = 255;
                        if (tg < 0) tg = 0; if (tg > 255) tg = 255;
                        if (tb < 0) tb = 0; if (tb > 255) tb = 255;
                        if (br < 0) br = 0; if (br > 255) br = 255;
                        if (bg < 0) bg = 0; if (bg > 255) bg = 255;
                        if (bb < 0) bb = 0; if (bb > 255) bb = 255;

                        out += "\x1B[38;2;"; fast_append_int(out, tr); out += ';'; fast_append_int(out, tg); out += ';'; fast_append_int(out, tb); out += 'm';
                        out += "\x1B[48;2;"; fast_append_int(out, br); out += ';'; fast_append_int(out, bg); out += ';'; fast_append_int(out, bb); out += 'm';
                        out += "\xE2\x96\x80";
                    }
                    out += "\x1B[0m\r\n";
                }
                });
        }

        for (auto& w : workers) w.join();

        std::string final_out;
        final_out.reserve((size_t)consoleW * (availRows + 2) * 24);

        final_out += "\x1B[H\x1B[33m";
        int titleLen = 18;
        int pad = (consoleW - titleLen) / 2;
        if (pad < 0) pad = 0;
        final_out.append(pad, ' ');
        final_out += "  CONSOLE PORTRAIT  ";
        if (twerk)          final_out += "   [TWERK]";
        if (sway && !twerk) final_out += "   [SWAY]";
        if (fireMode == 1)  final_out += "   [FIRE]";
        if (fireMode == 3)  final_out += "   [BLACKFIRE]";
        final_out += "\x1B[0m\r\n";

        for (unsigned int tid = 0; tid < num_threads; ++tid)
            final_out += thread_outputs[tid];

        final_out += "\x1B[0m";


        fwrite(final_out.data(), 1, final_out.size(), stdout);
        fflush(stdout);
        };

    if (!twerk && !sway && fireMode == 0) {
        render(0.0f);
        std::cout << "\nPress Enter to exit...";
        std::cin.get();
    }
    else {
        float t = 0.0f;
        while (!_kbhit()) {
            render(t);
            t += 0.04f;

        }
        _getch();
        std::cout << "\n";
    }

    GdiplusShutdown(token);
    return 0;
}