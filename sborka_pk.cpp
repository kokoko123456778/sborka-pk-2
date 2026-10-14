// =====================================================================
//  СБОРКА ПК — симулятор мастерской по сборке компьютеров (3D)
//  C++ + SDL для ccdroid / C4droid. Один файл, внешних библиотек нет.
//  Если не собирается: ниже поставь SDL_MODE 1 (SDL 1.2) или 2 (SDL2).
// =====================================================================
#ifndef SDL_MODE
#define SDL_MODE 0            // 0 = авто, 1 = SDL 1.2, 2 = SDL2
#endif
#if SDL_MODE == 0
  #if defined(__has_include)
    #if __has_include(<SDL2/SDL.h>)
      #undef SDL_MODE
      #define SDL_MODE 2
    #endif
  #endif
#endif
#if SDL_MODE == 0
  #undef SDL_MODE
  #define SDL_MODE 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#if defined(__GNUC__) && !defined(__clang__) && !defined(__OPTIMIZE__)
  #pragma GCC optimize("O2")      // если среда собирает без оптимизации, включаем её для этого файла
#endif
#include <vector>
#include <map>
#if defined(__unix__) || defined(__linux__) || defined(__ANDROID__) || defined(__APPLE__)
  #include <unistd.h>
  #define HAVE_GETCWD 1
#endif
#if SDL_MODE == 2
  #include <SDL2/SDL.h>
#else
  #include <SDL/SDL.h>
#endif

// ---------- Утилиты ----------
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float fminv(float a, float b) { return a < b ? a : b; }
static inline float fmaxv(float a, float b) { return a > b ? a : b; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline int clampi(int v, int a, int b) { return v < a ? a : (v > b ? b : v); }
static float frand(float a, float b) { return a + (b - a) * (rand() / (float)RAND_MAX); }

struct V3 { float x, y, z; };
static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline V3 operator+(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 operator-(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 operator*(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline V3 vnorm(V3 a) { float l = sqrtf(dot(a, a)); if (l < 1e-8f) l = 1; return a * (1.0f / l); }

typedef uint32_t u32;
static inline u32 RGB(int r, int g, int b) {
  r = clampi(r, 0, 255); g = clampi(g, 0, 255); b = clampi(b, 0, 255);
  return ((u32)r << 16) | ((u32)g << 8) | (u32)b;
}
static inline int CR(u32 c) { return (c >> 16) & 255; }
static inline int CG(u32 c) { return (c >> 8) & 255; }
static inline int CB(u32 c) { return c & 255; }
static u32 mixc(u32 a, u32 b, float t) {
  if (t <= 0) return a;
  if (t >= 1) return b;
  return RGB((int)(CR(a) + (CR(b) - CR(a)) * t), (int)(CG(a) + (CG(b) - CG(a)) * t),
             (int)(CB(a) + (CB(b) - CB(a)) * t));
}
static u32 shadec(u32 c, float f) { return RGB((int)(CR(c) * f), (int)(CG(c) * f), (int)(CB(c) * f)); }

// ---------- Экран и буферы ----------
static int SW = 800, SH = 480, IW = 400, IH = 240, fs = 2;
static int LW = 800, LH = 480;      // логический (горизонтальный) размер экрана
static bool rotMode = false;        // экран вертикальный -> картинку поворачиваем
static int rotDir = 1;              // 1 = по часовой, -1 = против
static int quality = 1;              // 0 низкая, 1 средняя, 2 высокая (разрешение картинки)
static bool aaOn = true;             // сглаживание краёв (можно выключить в меню, если телефон тормозит)
static int wheelN = 10;              // число граней у колёс и фар (растёт с уровнем графики)
static float gProf3D = 0, gProfPr = 0;   // мс за кадр: 3D-превью и вывод на экран (для счётчика FPS)
static bool soundOn = true;           // звук мотора
static bool fpsOn = true;            // счётчик FPS в углу экрана
static bool gDry = false;            // пробный проход: считаем, где фары относительно поверхности, ничего не рисуя
static bool autoPerf = true;         // автоматически упрощать картинку, если телефон не успевает
static int gPerf = 0;                // 0 - полная картинка, 1 - без сглаживания и суперсэмплинга, 2 - ещё и проще колёса, 3 - ещё и картинка на ступень мельче (только пока не хватает скорости)
static int gResDrop = 0;              // на сколько ступеней качества временно уменьшено разрешение (авто-режим, уровень 3)
static bool gApplyQ = false;
static int insL = 0, insR = 0;      // поля экрана: сколько физ. пикселей не видно с начала/конца оси X картинки
static u32 *fb = NULL;
static float *zb = NULL;
static int clipX0, clipY0, clipX1, clipY1;
static float focal = 300, vcx = 0, vcy = 0;
static u32 fogCol = 0;
static float fogA = 1e9f, fogB = 2e9f;
static float gTime = 0;

// ---------- Ввод ----------
struct Ev { int type, x, y; };   // 0 выход, 1 нажатие, 2 отпускание, 3 движение, 4 назад
struct Ptr {
  bool down, pressed, clicked;
  int x, y, sx, sy, cx, cy, dx, dy;
};
static Ptr P;

// ---------- Бэкенд SDL ----------
#if SDL_MODE == 2
static SDL_Window *gWin = NULL;
static SDL_Renderer *gRen = NULL;
static SDL_Texture *gTex = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  gWin = SDL_CreateWindow("PCBuilder", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
  if (!gWin) return false;
  SDL_GetWindowSize(gWin, &SW, &SH);
  gRen = SDL_CreateRenderer(gWin, -1, 0);
  if (!gRen) return false;
  return true;
}
static void gfxTex() {
  if (gTex) SDL_DestroyTexture(gTex);
  gTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IW, IH);
}
static void gfxResize() {}
static void gfxPresent() {
  SDL_UpdateTexture(gTex, NULL, fb, IW * 4);
  SDL_RenderClear(gRen);
  if (rotMode) {
    SDL_Rect d;
    int avail = SH - insL - insR;
    int cy = rotDir > 0 ? insL + avail / 2 : SH - insL - avail / 2;
    d.w = avail; d.h = SW; d.x = SW / 2 - d.w / 2; d.y = cy - d.h / 2;
    SDL_RenderCopyEx(gRen, gTex, NULL, &d, rotDir > 0 ? 90.0 : 270.0, NULL, SDL_FLIP_NONE);
  } else {
    SDL_Rect d;
    d.x = insL; d.y = 0; d.w = SW - insL - insR; d.h = SH;
    SDL_RenderCopy(gRen, gTex, NULL, &d);
  }
  SDL_RenderPresent(gRen);
}
static void gfxQuit() { SDL_Quit(); }
#else
static SDL_Surface *gScr = NULL;
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  gScr = SDL_SetVideoMode(0, 0, 32, SDL_SWSURFACE | SDL_FULLSCREEN);
  if (!gScr) gScr = SDL_SetVideoMode(800, 480, 32, SDL_SWSURFACE);
  if (!gScr) return false;
  SW = gScr->w;
  SH = gScr->h;
  return true;
}
static void gfxTex() {}
static u32 *convBuf = NULL, *rotBuf = NULL;
static int *axMap = NULL;
static void gfxResize() { free(convBuf); convBuf = NULL; free(rotBuf); rotBuf = NULL; }
static void gfxPresent() {
  if (SDL_MUSTLOCK(gScr)) SDL_LockSurface(gScr);
  SDL_PixelFormat *f = gScr->format;
  int bpp = f->BytesPerPixel;
  int n = IW * IH;
  if (!axMap) axMap = (int *)malloc(sizeof(int) * SW);
  const u32 *src = fb;
  bool native = bpp == 4 && f->Rshift == 16 && f->Gshift == 8 && f->Bshift == 0 && f->Rloss == 0 && f->Gloss == 0 && f->Bloss == 0;
  if (!native) {       // перевод в формат экрана (только если он не 0x00RRGGBB)
    if (!convBuf) convBuf = (u32 *)malloc(sizeof(u32) * n);
    for (int i = 0; i < n; i++) {
      u32 c = fb[i];
      convBuf[i] = (((c >> 16) & 255) >> f->Rloss << f->Rshift) | (((c >> 8) & 255) >> f->Gloss << f->Gshift) |
                   ((c & 255) >> f->Bloss << f->Bshift);
    }
    src = convBuf;
  }
  int PW = rotMode ? SH : SW, avail = imax(1, PW - insL - insR);
  if (rotMode) {       // транспонируем блоками: строки rotBuf = столбцы картинки (читать потом подряд)
    if (!rotBuf) rotBuf = (u32 *)malloc(sizeof(u32) * n);
    const int B = 16;
    for (int vb = 0; vb < IH; vb += B)
      for (int ub = 0; ub < IW; ub += B) {
        int ve = imin(vb + B, IH), ue = imin(ub + B, IW);
        for (int v = vb; v < ve; v++) {
          const u32 *sr = src + v * IW;
          for (int u = ub; u < ue; u++) rotBuf[u * IH + v] = sr[u];
        }
      }
    for (int x = 0; x < SW; x++) axMap[x] = imin((rotDir > 0 ? SW - 1 - x : x) * IH / SW, IH - 1);
  } else {
    for (int x = 0; x < SW; x++) axMap[x] = clampi((x - insL) * IW / avail, 0, IW - 1);
  }
  unsigned char *base = (unsigned char *)gScr->pixels;
  int prevBy = -1;
  unsigned char *prevRow = NULL;
  for (int y = 0; y < SH; y++) {
    unsigned char *row = base + y * gScr->pitch;
    int by;
    const u32 *sr;
    if (!rotMode) {
      by = imin(y * IH / SH, IH - 1);
      sr = src + by * IW;
    } else {
      int pu = rotDir > 0 ? y : SH - 1 - y;
      if (pu < insL || pu >= SH - insR) { memset(row, 0, SW * bpp); prevBy = -1; continue; }   // поле экрана
      by = clampi((pu - insL) * IW / avail, 0, IW - 1);
      sr = rotBuf + by * IH;
    }
    if (by == prevBy) { memcpy(row, prevRow, SW * bpp); continue; }
    if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < SW; x++) d[x] = sr[axMap[x]]; }
    else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < SW; x++) d[x] = (uint16_t)sr[axMap[x]]; }
    else for (int x = 0; x < SW; x++) { u32 v = sr[axMap[x]]; row[x * 3] = v & 255; row[x * 3 + 1] = (v >> 8) & 255; row[x * 3 + 2] = (v >> 16) & 255; }
    if (!rotMode) {   // поля слева и справа
      if (bpp == 4) { u32 *d = (u32 *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
      else if (bpp == 2) { uint16_t *d = (uint16_t *)row; for (int x = 0; x < imin(insL, SW); x++) d[x] = 0; for (int x = imax(0, SW - insR); x < SW; x++) d[x] = 0; }
    }
    prevBy = by;
    prevRow = row;
  }
  if (SDL_MUSTLOCK(gScr)) SDL_UnlockSurface(gScr);
  SDL_Flip(gScr);
}
static void gfxQuit() { SDL_Quit(); }
#endif

static bool pollEv(Ev &e) {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT: e.type = 0; e.x = e.y = 0; return true;
      case SDL_MOUSEBUTTONDOWN: e.type = 1; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEBUTTONUP: e.type = 2; e.x = ev.button.x; e.y = ev.button.y; return true;
      case SDL_MOUSEMOTION: e.type = 3; e.x = ev.motion.x; e.y = ev.motion.y; return true;
      case SDL_KEYDOWN:
        if (ev.key.keysym.sym == SDLK_ESCAPE) { e.type = 4; e.x = e.y = 0; return true; }
        break;
#if SDL_MODE == 2
      case SDL_APP_WILLENTERBACKGROUND:
      case SDL_APP_TERMINATING:
      case SDL_APP_LOWMEMORY:
        e.type = 5; e.x = e.y = 0; return true;
#else
      case SDL_ACTIVEEVENT:
        if (ev.active.gain == 0) { e.type = 5; e.x = e.y = 0; return true; }
        break;
#endif
      default: break;
    }
  }
  return false;
}

// окно на Android часто сначала меньше экрана (пока прячутся системные панели), а потом вырастает:
// если не пересчитать размер, справа остаётся чёрная полоса
static bool gfxCheckResize() {
#if SDL_MODE == 2
  int w = 0, h = 0; SDL_GetWindowSize(gWin, &w, &h);
  if (w > 0 && h > 0 && (w != SW || h != SH)) { SW = w; SH = h; return true; }
#endif
  return false;
}

static void mapPt(int x, int y, int &mx, int &my) {
  int pu, pv;   // pu - физ. координата вдоль оси X картинки, pv - вдоль оси Y
  if (!rotMode) { pu = x; pv = y; }
  else if (rotDir > 0) { pu = y; pv = SW - 1 - x; }
  else { pu = SH - 1 - y; pv = x; }
  int PW = rotMode ? SH : SW, PH = rotMode ? SW : SH;
  int avail = imax(1, PW - insL - insR);
  mx = clampi((pu - insL) * IW / avail, 0, IW - 1);
  my = clampi(pv * IH / imax(1, PH), 0, IH - 1);
}
static void computeSize() {
  static const int T[3] = {360, 480, 600};
  int md = imax(1, imin(SW, SH));
  int qe = imax(0, quality - gResDrop);
  IW = imax(200, LW * T[qe] / md);
  IH = imax(120, LH * T[qe] / md);
  fs = imax(1, imin(IW, IH) / 170);
}
static bool allocBuffers() {
  free(fb); free(zb);
  fb = (u32 *)malloc(sizeof(u32) * IW * IH);
  zb = (float *)malloc(sizeof(float) * IW * IH);
  if (!fb || !zb) return false;
  memset(fb, 0, sizeof(u32) * IW * IH);
  gfxResize();
  gfxTex();
  return true;
}
// Настройки экрана (поля) хранятся отдельно от игры
// Папка для файлов игры. На Android рабочая папка недоступна для записи, поэтому пишем во внутреннюю память приложения
// (иначе деньги и прогресс не сохранялись). На других системах - рядом с игрой, как раньше.
static char gBase[400] = "";
static char gPathBuf[4][520];
static int gPathI = 0;
static const char *pth(const char *name) {
  gPathI = (gPathI + 1) & 3;
  snprintf(gPathBuf[gPathI], sizeof gPathBuf[0], "%s%s", gBase, name);
  return gPathBuf[gPathI];
}
static const char *CFGF = "pcbuilder.cfg";
static int autoInset() { return rotMode ? SH * 19 / 1000 : 0; }
static void saveCfg() {
  FILE *f = fopen(pth(CFGF), "w");
  if (!f) return;
  fprintf(f, "%d %d %d %d %d %d %d %d 2\n", insL, insR, rotDir, quality, aaOn ? 1 : 0, fpsOn ? 1 : 0, autoPerf ? 1 : 0, soundOn ? 1 : 0);
  fclose(f);
}
static void loadCfg() {
  FILE *f = fopen(pth(CFGF), "r");
  if (!f) return;
  int a = 0, b = 0, d = 1, q = 1, aa = 1, fp = 1, ap = 1, so = 1, ver = 0;
  int got = fscanf(f, "%d %d %d %d %d %d %d %d %d", &a, &b, &d, &q, &aa, &fp, &ap, &so, &ver);
  if (got < 9 || ver < 2) { a = 0; b = 0; }          // старые поля экрана не берём: приложение теперь рисует на весь экран
  if (got >= 8) soundOn = so != 0;
  if (got >= 3) { insL = clampi(a, 0, 260); insR = clampi(b, 0, 260); rotDir = d < 0 ? -1 : 1; }
  if (got >= 4) quality = clampi(q, 0, 2);
  if (got >= 5) aaOn = aa != 0;
  if (got >= 6) fpsOn = fp != 0;
  if (got >= 7) autoPerf = ap != 0;
  fclose(f);
}

// ---------- Шрифт 5x7 (заглавные, латиница + кириллица) ----------
static const unsigned char FLAT[26][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
  {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x04,0x04,0x04,0x04,0x04,0x0E},
  {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x19,0x15,0x13,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x11,0x11,0x15,0x12,0x0D},{0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
  {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E},{0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},{0x11,0x11,0x11,0x15,0x15,0x1B,0x11},{0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
  {0x11,0x11,0x0A,0x04,0x04,0x04,0x04},{0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}};
static const unsigned char FDIG[10][7] = {
  {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E},{0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},{0x0E,0x11,0x01,0x02,0x04,0x08,0x1F},
  {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},{0x02,0x06,0x0A,0x12,0x1F,0x02,0x02},{0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
  {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E},{0x1F,0x01,0x02,0x04,0x08,0x08,0x08},{0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E},
  {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}};
// А Б В Г Д Е Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я
static const unsigned char FCYR[32][7] = {
  {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11},{0x1F,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
  {0x1F,0x10,0x10,0x10,0x10,0x10,0x10},{0x06,0x0A,0x0A,0x0A,0x0A,0x1F,0x11},{0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F},
  {0x15,0x15,0x15,0x0E,0x15,0x15,0x15},{0x0E,0x11,0x01,0x06,0x01,0x11,0x0E},{0x11,0x11,0x13,0x15,0x19,0x11,0x11},
  {0x0A,0x11,0x13,0x15,0x19,0x11,0x11},{0x11,0x12,0x14,0x18,0x14,0x12,0x11},{0x07,0x09,0x09,0x09,0x09,0x09,0x11},
  {0x11,0x1B,0x15,0x15,0x11,0x11,0x11},{0x11,0x11,0x11,0x1F,0x11,0x11,0x11},{0x0E,0x11,0x11,0x11,0x11,0x11,0x0E},
  {0x1F,0x11,0x11,0x11,0x11,0x11,0x11},{0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},{0x0E,0x11,0x10,0x10,0x10,0x11,0x0E},
  {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},{0x11,0x11,0x11,0x0F,0x01,0x11,0x0E},{0x04,0x0E,0x15,0x15,0x15,0x0E,0x04},
  {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},{0x12,0x12,0x12,0x12,0x12,0x1F,0x01},{0x11,0x11,0x11,0x0F,0x01,0x01,0x01},
  {0x15,0x15,0x15,0x15,0x15,0x15,0x1F},{0x15,0x15,0x15,0x15,0x15,0x1F,0x01},{0x18,0x08,0x08,0x0E,0x09,0x09,0x0E},
  {0x11,0x11,0x11,0x19,0x15,0x15,0x19},{0x10,0x10,0x10,0x1E,0x11,0x11,0x1E},{0x0E,0x11,0x01,0x0F,0x01,0x11,0x0E},
  {0x12,0x15,0x15,0x1D,0x15,0x15,0x12},{0x0F,0x11,0x11,0x0F,0x05,0x09,0x11}};
static const unsigned char FYO[7] = {0x0A,0x1F,0x10,0x1E,0x10,0x10,0x1F};
struct PG { char c; unsigned char r[7]; };
static const PG FPUN[] = {
  {'.',{0,0,0,0,0,0x0C,0x0C}},{',',{0,0,0,0,0x0C,0x04,0x08}},{':',{0,0x0C,0x0C,0,0x0C,0x0C,0}},
  {'!',{0x04,0x04,0x04,0x04,0x04,0,0x04}},{'?',{0x0E,0x11,0x01,0x02,0x04,0,0x04}},{'-',{0,0,0,0x1F,0,0,0}},
  {'+',{0,0x04,0x04,0x1F,0x04,0x04,0}},{'/',{0x01,0x01,0x02,0x04,0x08,0x10,0x10}},
  {'%',{0x18,0x19,0x02,0x04,0x08,0x13,0x03}},{'$',{0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}},
  {'(',{0x02,0x04,0x08,0x08,0x08,0x04,0x02}},{')',{0x08,0x04,0x02,0x02,0x02,0x04,0x08}},
  {'<',{0x02,0x04,0x08,0x10,0x08,0x04,0x02}},{'>',{0x08,0x04,0x02,0x01,0x02,0x04,0x08}},
  {'=',{0,0,0x1F,0,0x1F,0,0}},{'|',{0x04,0x04,0x04,0x04,0x04,0x04,0x04}},{'_',{0,0,0,0,0,0,0x1F}},
  {'\'',{0x04,0x04,0x08,0,0,0,0}},{'*',{0,0x15,0x0E,0x1F,0x0E,0x15,0}},{'#',{0x0A,0x1F,0x0A,0x0A,0x1F,0x0A,0}},
  {'~',{0,0,0x08,0x15,0x02,0,0}},{'"',{0x0A,0x0A,0x0A,0,0,0,0}}};
static const int NPUN = sizeof(FPUN) / sizeof(FPUN[0]);

static const unsigned char *glyph(int cp) {
  static const unsigned char blank[7] = {0, 0, 0, 0, 0, 0, 0};
  if (cp >= 0x430 && cp <= 0x44F) cp -= 0x20;
  if (cp == 0x451) cp = 0x401;
  if (cp >= 'a' && cp <= 'z') cp -= 32;
  if (cp >= 'A' && cp <= 'Z') return FLAT[cp - 'A'];
  if (cp >= '0' && cp <= '9') return FDIG[cp - '0'];
  if (cp >= 0x410 && cp <= 0x42F) return FCYR[cp - 0x410];
  if (cp == 0x401) return FYO;
  if (cp < 128)
    for (int i = 0; i < NPUN; i++)
      if (FPUN[i].c == (char)cp) return FPUN[i].r;
  return blank;
}
static int u8next(const char *&p) {
  unsigned char c = (unsigned char)*p;
  if (c < 0x80) { p++; return c; }
  if ((c & 0xE0) == 0xC0 && p[1] && (p[1] & 0xC0) == 0x80) { int r = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; return r; }
  if ((c & 0xF0) == 0xE0 && p[1] && p[2] && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { int r = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; return r; }
  p++;
  return '?';
}
static int textW(const char *s, int sc) {
  int n = 0;
  while (*s) { u8next(s); n++; }
  return n * 6 * sc;
}
static inline void putpx(int x, int y, u32 c) {
  if ((unsigned)x < (unsigned)IW && (unsigned)y < (unsigned)IH) fb[y * IW + x] = c;
}
static void fillRect(int x, int y, int w, int h, u32 c) {
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) r[i] = c;
  }
}
static void fillRectA(int x, int y, int w, int h, u32 c, int a) {   // a: 0..255
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  int ia = 255 - a;
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) {
      u32 d = r[i];
      r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
static void fillCircle(int cx, int cy, int r, u32 c, int a) {
  int ia = 255 - a;
  for (int j = -r; j <= r; j++) {
    int y = cy + j;
    if ((unsigned)y >= (unsigned)IH) continue;
    int w = (int)sqrtf((float)(r * r - j * j));
    for (int i = -w; i <= w; i++) {
      int x = cx + i;
      if ((unsigned)x >= (unsigned)IW) continue;
      u32 d = fb[y * IW + x];
      fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255);
    }
  }
}
// align: 0 слева, 1 по центру, 2 справа
static void drawText(int x, int y, const char *s, int sc, u32 col, int align) {
  int w = textW(s, sc);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  while (*s) {
    int cp = u8next(s);
    const unsigned char *g = glyph(cp);
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++)
        if (g[r] & (0x10 >> c)) fillRect(x + c * sc, y + r * sc, sc, sc, col);
    x += 6 * sc;
  }
}
static void drawTextSh(int x, int y, const char *s, int sc, u32 col, int align) {
  drawText(x + sc, y + sc, s, sc, RGB(0, 0, 0), align);
  drawText(x, y, s, sc, col, align);
}
// ---------- Камера и растеризатор ----------
static V3 camEye, camF, camR, camU;
static void setCamera(V3 eye, V3 target) {
  camEye = eye;
  camF = vnorm(target - eye);
  camR = vnorm(cross(v3(0, 1, 0), camF));
  camU = cross(camF, camR);
}
static inline V3 toView(V3 p) {
  V3 d = p - camEye;
  return v3(dot(d, camR), dot(d, camU), dot(d, camF));
}
static bool projectPt(V3 p, int &sx, int &sy, float &z) {
  V3 v = toView(p);
  z = v.z;
  if (v.z < 0.5f) return false;
  sx = (int)(vcx + v.x * focal / v.z);
  sy = (int)(vcy - v.y * focal / v.z);
  return true;
}

static void rasterTri(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, u32 col) {
  // сортируем вершины по Y: a <= b <= c
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay);
  float dxac = (cx - ax) * iac, dwac = (cw - aw) * iac;
  float dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f;
    float xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f;
    float w = wl + dw * (xs + 0.5f - xl);
    float *zp = zb + y * IW + xs;
    u32 *fp = fb + y * IW + xs;
    int cnt = xe - xs + 1;
    for (; cnt >= 4; cnt -= 4, zp += 4, fp += 4, w += dw * 4.0f) {      // по 4 пикселя за проход
      float w1 = w + dw, w2 = w1 + dw, w3 = w2 + dw;
      if (w > zp[0]) { zp[0] = w; fp[0] = col; }
      if (w1 > zp[1]) { zp[1] = w1; fp[1] = col; }
      if (w2 > zp[2]) { zp[2] = w2; fp[2] = col; }
      if (w3 > zp[3]) { zp[3] = w3; fp[3] = col; }
    }
    for (; cnt > 0; cnt--, zp++, fp++, w += dw) {
      if (w > *zp) { *zp = w; *fp = col; }
    }
  }
}

// Мягкая тень: умножает цвет пикселей под многоугольником (k/256), глубину не пишет
static void rasterTriShadow(float x0, float y0, float w0, float x1, float y1, float w1, float x2, float y2, float w2, int k) {
  float ax = x0, ay = y0, aw = w0, bx = x1, by = y1, bw = w1, cx = x2, cy = y2, cw = w2, t;
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (by > cy) { t = bx; bx = cx; cx = t; t = by; by = cy; cy = t; t = bw; bw = cw; cw = t; }
  if (ay > by) { t = ax; ax = bx; bx = t; t = ay; ay = by; by = t; t = aw; aw = bw; bw = t; }
  if (cy - ay < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(ay - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(cy - 0.498f));
  if (yS > yE) return;
  float iac = 1.0f / (cy - ay), dxac = (cx - ax) * iac, dwac = (cw - aw) * iac, dxab = 0, dwab = 0, dxbc = 0, dwbc = 0;
  if (by - ay > 1e-6f) { float i = 1.0f / (by - ay); dxab = (bx - ax) * i; dwab = (bw - aw) * i; }
  if (cy - by > 1e-6f) { float i = 1.0f / (cy - by); dxbc = (cx - bx) * i; dwbc = (cw - bw) * i; }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, xl = ax + dxac * (py - ay), wl = aw + dwac * (py - ay), xr, wr;
    if (py < by) { xr = ax + dxab * (py - ay); wr = aw + dwab * (py - ay); }
    else { xr = bx + dxbc * (py - by); wr = bw + dwbc * (py - by); }
    if (xl > xr) { t = xl; xl = xr; xr = t; t = wl; wl = wr; wr = t; }
    int xs = imax(clipX0, (int)ceilf(xl - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(xr - 0.498f));
    if (xs > xe) continue;
    float span = xr - xl, dw = span > 1e-6f ? (wr - wl) / span : 0.0f, w = wl + dw * (xs + 0.5f - xl);
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw) {
      float zv = zb[idx];
      if (zv > 0.0f && w > zv * 0.9985f) {
        u32 c = fb[idx];
        fb[idx] = ((((c >> 16) & 255u) * (u32)k >> 8) << 16) | ((((c >> 8) & 255u) * (u32)k >> 8) << 8) | (((c & 255u) * (u32)k) >> 8);
      }
    }
  }
}
static void drawPolyView(const V3 *in, int n, u32 col, int shadowK = 0) {
  if (gDry) return;
  V3 v[32];
  int m = 0;
  const float NZ = 0.3f;
  for (int i = 0; i < n; i++) {
    V3 a = in[i], b = in[(i + 1) % n];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) v[m++] = a;
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m++] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
    }
  }
  if (m < 3) return;
  float sx[32], sy[32], sw[32], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    sx[i] = vcx + v[i].x * focal * iz;
    sy[i] = vcy - v[i].y * focal * iz;
    sw[i] = iz;
    zs += v[i].z;
  }
  zs /= m;
  if (shadowK) {
    for (int i = 1; i + 1 < m; i++) rasterTriShadow(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], shadowK);
    return;
  }
  if (zs > fogA) col = mixc(col, fogCol, (zs - fogA) / (fogB - fogA));
  for (int i = 1; i + 1 < m; i++) rasterTri(sx[0], sy[0], sw[0], sx[i], sy[i], sw[i], sx[i + 1], sy[i + 1], sw[i + 1], col);
}

static V3 LDIR;
static float gLight = 1.0f, gNight = 0.0f;        // освещённость мира (1 - день) и «ночность» (0 день ... 1 ночь)
static float lightOf(V3 n) {
  float diff = fmaxv(0.0f, dot(n, LDIR));
  float amb = n.y < -0.4f ? 0.30f : 0.38f + 0.14f * n.y;
  return (amb + 0.62f * diff) * gLight;
}
// Глянец кодируется в старшем байте цвета (0 - матовый, 255 - зеркальный)
#define SHINE(c, sh) (((c) & 0xFFFFFFu) | ((u32)(sh) << 24))

// Грань с известной нормалью: отсечение обратных, свет, отрисовка
// Растеризатор с плавным цветом по треугольнику (освещение интерполируется между вершинами)
// вершина: [0]=x, [1]=y, [2]=1/z, [3]=R, [4]=G, [5]=B
static void rasterTriG(float *a, float *b, float *c) {
  float *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, dr = (R[2] - L[2]) * inv, dg = (R[3] - L[3]) * inv, db = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, rr = L[2] + dr * off, gg = L[3] + dg * off, bb = L[4] + db * off;
    int idx = y * IW + xs;
    {
      float n1 = (float)(xe - xs), r1 = rr + dr * n1, g1 = gg + dg * n1, b1 = bb + db * n1;
      if (rr >= 0.0f && rr <= 255.0f && r1 >= 0.0f && r1 <= 255.0f && gg >= 0.0f && gg <= 255.0f && g1 >= 0.0f && g1 <= 255.0f &&
          bb >= 0.0f && bb <= 255.0f && b1 >= 0.0f && b1 <= 255.0f) {      // быстрый путь: цвет не выходит за 0..255
        for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
          if (w > zb[idx]) { zb[idx] = w; fb[idx] = (u32)(((int)rr << 16) | ((int)gg << 8) | (int)bb); }
        }
        continue;
      }
    }
    for (int x = xs; x <= xe; x++, idx++, w += dw, rr += dr, gg += dg, bb += db) {
      if (w > zb[idx]) {
        zb[idx] = w;
        int ir = (int)rr, ig = (int)gg, ib = (int)bb;
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir);
        ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig);
        ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
// Многоугольник с цветами в вершинах: отсечение по ближней плоскости, туман, веер треугольников
// Треугольник с текстурой (ливрея): атрибуты [0]=x, [1]=y, [2]=1/z, [3]=u, [4]=v, [5]=освещённость; tc - цвета уровня текстуры, rx0..ry1 - рамка проекции на этом уровне
static void rasterTriT(const float *a0, const float *b0, const float *c0, const u32 *tc, int stride, int rx0, int ry0, int rx1, int ry1, float invs, float fogT) {
  const float *a = a0, *b = b0, *c = c0, *t;
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (b[1] > c[1]) { t = b; b = c; c = t; }
  if (a[1] > b[1]) { t = a; a = b; b = t; }
  if (c[1] - a[1] < 1e-4f) return;
  int yS = imax(clipY0, (int)ceilf(a[1] - 0.502f)), yE = imin(clipY1 - 1, (int)floorf(c[1] - 0.498f));
  if (yS > yE) return;
  static const int I[5] = {0, 2, 3, 4, 5};
  float iac = 1.0f / (c[1] - a[1]), dac[5], dab[5] = {0, 0, 0, 0, 0}, dbc[5] = {0, 0, 0, 0, 0};
  bool hasAB = b[1] - a[1] > 1e-6f, hasBC = c[1] - b[1] > 1e-6f;
  float iab = hasAB ? 1.0f / (b[1] - a[1]) : 0.0f, ibc = hasBC ? 1.0f / (c[1] - b[1]) : 0.0f;
  for (int m = 0; m < 5; m++) {
    dac[m] = (c[I[m]] - a[I[m]]) * iac;
    if (hasAB) dab[m] = (b[I[m]] - a[I[m]]) * iab;
    if (hasBC) dbc[m] = (c[I[m]] - b[I[m]]) * ibc;
  }
  float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbl = (float)(fogCol & 255);
  for (int y = yS; y <= yE; y++) {
    float py = y + 0.5f, L[5], R[5];
    for (int m = 0; m < 5; m++) {
      L[m] = a[I[m]] + dac[m] * (py - a[1]);
      R[m] = py < b[1] ? a[I[m]] + dab[m] * (py - a[1]) : b[I[m]] + dbc[m] * (py - b[1]);
    }
    if (L[0] > R[0]) for (int m = 0; m < 5; m++) { float q = L[m]; L[m] = R[m]; R[m] = q; }
    int xs = imax(clipX0, (int)ceilf(L[0] - 0.502f)), xe = imin(clipX1 - 1, (int)floorf(R[0] - 0.498f));
    if (xs > xe) continue;
    float span = R[0] - L[0], inv = span > 1e-6f ? 1.0f / span : 0.0f, off = xs + 0.5f - L[0];
    float dw = (R[1] - L[1]) * inv, du = (R[2] - L[2]) * inv, dv = (R[3] - L[3]) * inv, dl = (R[4] - L[4]) * inv;
    float w = L[1] + dw * off, u = L[2] + du * off, v = L[3] + dv * off, l = L[4] + dl * off;
    int idx = y * IW + xs;
    for (int x = xs; x <= xe; x++, idx++, w += dw, u += du, v += dv, l += dl) {
      if (w > zb[idx]) {
        int tx = (int)(u * invs), ty = (int)(v * invs);
        tx = tx < rx0 ? rx0 : (tx > rx1 ? rx1 : tx);
        ty = ty < ry0 ? ry0 : (ty > ry1 ? ry1 : ty);
        u32 pc = tc[ty * stride + tx];
        float lk = l < 0.0f ? 0.0f : (l > 1.3f ? 1.3f : l);
        float cr = ((pc >> 16) & 255) * lk, cg = ((pc >> 8) & 255) * lk, cb = (pc & 255) * lk;
        if (fogT > 0.0f) { cr += (fr - cr) * fogT; cg += (fg - cg) * fogT; cb += (fbl - cb) * fogT; }
        int ir = cr > 255.0f ? 255 : (int)cr, ig = cg > 255.0f ? 255 : (int)cg, ib = cb > 255.0f ? 255 : (int)cb;
        zb[idx] = w; fb[idx] = (u32)((ir << 16) | (ig << 8) | ib);
      }
    }
  }
}
static void drawPolyViewG(const V3 *in, const float (*cin)[3], int n) {
  if (gDry) return;
  if (n == 3 && in[0].z >= 0.3f && in[1].z >= 0.3f && in[2].z >= 0.3f) {      // целый треугольник перед камерой: без отсечения
    float vt3[3][6];
    float zs3 = (in[0].z + in[1].z + in[2].z) * (1.0f / 3.0f);
    for (int i = 0; i < 3; i++) {
      float iz = 1.0f / in[i].z;
      vt3[i][0] = vcx + in[i].x * focal * iz; vt3[i][1] = vcy - in[i].y * focal * iz; vt3[i][2] = iz;
      vt3[i][3] = cin[i][0]; vt3[i][4] = cin[i][1]; vt3[i][5] = cin[i][2];
    }
    if (zs3 > fogA) {
      float t = clampf((zs3 - fogA) / (fogB - fogA), 0.0f, 1.0f);
      float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
      for (int i = 0; i < 3; i++) { vt3[i][3] += (fr - vt3[i][3]) * t; vt3[i][4] += (fg - vt3[i][4]) * t; vt3[i][5] += (fbb - vt3[i][5]) * t; }
    }
    {   // мелкий треугольник (до ~20 пикселей): плавный цвет не виден, красим одним средним цветом - в разы быстрее
      float ar2 = fabsf((vt3[1][0] - vt3[0][0]) * (vt3[2][1] - vt3[0][1]) - (vt3[2][0] - vt3[0][0]) * (vt3[1][1] - vt3[0][1]));
      if (ar2 < 40.0f) {
        int ir = (int)((vt3[0][3] + vt3[1][3] + vt3[2][3]) * (1.0f / 3.0f)), ig = (int)((vt3[0][4] + vt3[1][4] + vt3[2][4]) * (1.0f / 3.0f)),
            ib = (int)((vt3[0][5] + vt3[1][5] + vt3[2][5]) * (1.0f / 3.0f));
        ir = ir < 0 ? 0 : (ir > 255 ? 255 : ir); ig = ig < 0 ? 0 : (ig > 255 ? 255 : ig); ib = ib < 0 ? 0 : (ib > 255 ? 255 : ib);
        rasterTri(vt3[0][0], vt3[0][1], vt3[0][2], vt3[1][0], vt3[1][1], vt3[1][2], vt3[2][0], vt3[2][1], vt3[2][2], (u32)((ir << 16) | (ig << 8) | ib));
        return;
      }
    }
    rasterTriG(vt3[0], vt3[1], vt3[2]);
    return;
  }
  V3 v[32];
  float cv[32][3];
  int m = 0;
  const float NZ = 0.3f;
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    V3 a = in[i], b = in[j];
    bool ia = a.z >= NZ, ib = b.z >= NZ;
    if (ia) { v[m] = a; cv[m][0] = cin[i][0]; cv[m][1] = cin[i][1]; cv[m][2] = cin[i][2]; m++; }
    if (ia != ib) {
      float t = (NZ - a.z) / (b.z - a.z);
      v[m] = v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, NZ);
      for (int k = 0; k < 3; k++) cv[m][k] = cin[i][k] + (cin[j][k] - cin[i][k]) * t;
      m++;
    }
  }
  if (m < 3) return;
  float vt[32][6], zs = 0;
  for (int i = 0; i < m; i++) {
    float iz = 1.0f / v[i].z;
    vt[i][0] = vcx + v[i].x * focal * iz;
    vt[i][1] = vcy - v[i].y * focal * iz;
    vt[i][2] = iz;
    vt[i][3] = cv[i][0]; vt[i][4] = cv[i][1]; vt[i][5] = cv[i][2];
    zs += v[i].z;
  }
  zs /= m;
  if (zs > fogA) {
    float t = clampf((zs - fogA) / (fogB - fogA), 0.0f, 1.0f);
    float fr = (float)((fogCol >> 16) & 255), fg = (float)((fogCol >> 8) & 255), fbb = (float)(fogCol & 255);
    for (int i = 0; i < m; i++) {
      vt[i][3] += (fr - vt[i][3]) * t; vt[i][4] += (fg - vt[i][4]) * t; vt[i][5] += (fbb - vt[i][5]) * t;
    }
  }
  for (int i = 1; i + 1 < m; i++) rasterTriG(vt[0], vt[i], vt[i + 1]);
}
// цвет в вершине: освещение по нормали вершины плюс глянец (те же формулы, что у плоской грани)
// Цвет «окружения» в направлении R: светлый горизонт и голубое небо сверху, тёмная дорога снизу
static void envColor(V3 R, float *o) {
  if (R.y >= 0) {
    float t = sqrtf(fminv(1.0f, R.y));
    o[0] = 238.0f + (40.0f - 238.0f) * t; o[1] = 244.0f + (96.0f - 244.0f) * t; o[2] = 250.0f + (196.0f - 250.0f) * t;
  } else {
    float t = fminv(1.0f, -R.y * 2.2f);
    o[0] = 120.0f + (22.0f - 120.0f) * t; o[1] = 124.0f + (24.0f - 124.0f) * t; o[2] = 130.0f + (28.0f - 130.0f) * t;
  }
}
// Глянец: отражение окружения (сильнее под скользящим углом, как у лака и стекла) и яркий блик солнца. c = r, g, b (0..255)
static void glossMix(V3 p, V3 nrm, int shi, float *c) {
  V3 vd = vnorm(camEye - p), hv = vnorm(vd + LDIR);
  float nd = dot(nrm, vd), fb = 1.0f - fmaxv(0.0f, nd), fr = fb * fb * fb, g = shi * (1.0f / 255.0f);
  V3 R = nrm * (2.0f * nd) - vd;
  float e[3];
  envColor(R, e);
  float kr = clampf(g * (0.30f + 0.70f * fr), 0.0f, 0.85f);
  c[0] += (e[0] - c[0]) * kr; c[1] += (e[1] - c[1]) * kr; c[2] += (e[2] - c[2]) * kr;
  float sb = fmaxv(0.0f, dot(nrm, hv)), s2 = sb * sb, s4 = s2 * s2, s8 = s4 * s4, s16 = s8 * s8;
  float k2 = clampf(s16 * s8 * s4 * g, 0.0f, 0.9f);
  c[0] += (255.0f - c[0]) * k2; c[1] += (255.0f - c[1]) * k2; c[2] += (255.0f - c[2]) * k2;
}
// цвет в вершине: освещение по нормали, затемнение у земли и глянец
static void shadeVertex(V3 p, V3 nrm, u32 col, int shi, float *o) {
  float l = lightOf(nrm);
  float c[3] = {((col >> 16) & 255) * l, ((col >> 8) & 255) * l, (col & 255) * l};
  if (shi) {
    float ao = 0.52f + 0.48f * clampf(p.y * (1.0f / 1.0f), 0.0f, 1.0f);     // низ кузова темнее: машина «стоит» на земле
    c[0] *= ao; c[1] *= ao; c[2] *= ao;
    glossMix(p, nrm, shi, c);
  }
  o[0] = c[0]; o[1] = c[1]; o[2] = c[2];
}
// то же для плоской грани: возвращает готовый цвет
static u32 shadeFlat(V3 fc, V3 nrm, u32 col, int shi, bool lit) {
  col &= 0xFFFFFFu;
  if (!shi) return lit ? shadec(col, lightOf(nrm)) : col;
  float l = lit ? lightOf(nrm) : 1.0f;
  float ao = lit ? 0.52f + 0.48f * clampf(fc.y * (1.0f / 1.0f), 0.0f, 1.0f) : 1.0f;
  float c[3] = {((col >> 16) & 255) * l * ao, ((col >> 8) & 255) * l * ao, (col & 255) * l * ao};
  glossMix(fc, nrm, shi, c);
  return RGB((int)clampf(c[0], 0.0f, 255.0f), (int)clampf(c[1], 0.0f, 255.0f), (int)clampf(c[2], 0.0f, 255.0f));
}
static void faceSmooth(const V3 *p, const V3 *nv, int n, V3 fnrm, u32 col) {
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(fnrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[8];
  float cc[8][3];
  for (int i = 0; i < n && i < 8; i++) { vv[i] = toView(p[i]); shadeVertex(p[i], nv[i], col, shi, cc[i]); }
  drawPolyViewG(vv, cc, n);
}
static void faceN(const V3 *p, int n, V3 nrm, u32 col, bool lit) {
  if (gDry) return;
  V3 fc = p[0];
  for (int i = 1; i < n; i++) fc = fc + p[i];
  fc = fc * (1.0f / n);
  if (dot(nrm, fc - camEye) >= 0) return;
  int shi = (int)((col >> 24) & 255u);
  col &= 0xFFFFFFu;
  V3 vv[24];
  for (int i = 0; i < n; i++) vv[i] = toView(p[i]);
  u32 c2 = shadeFlat(fc, nrm, col, shi, lit);
  drawPolyView(vv, n, c2);
}
static void quadFace(V3 c, V3 u, V3 v, V3 n, u32 col, bool lit) {
  V3 p[4];
  p[0] = c - u - v; p[1] = c + u - v; p[2] = c + u + v; p[3] = c - u + v;
  faceN(p, 4, n, col, lit);
}
// Параллелепипед: центр, полуразмеры, поворот вокруг Y
static void drawBox(V3 c, V3 h, float yaw, u32 col, bool lit) {
  if (gDry) return;
  float cs = cosf(yaw), sn = sinf(yaw);
  V3 ax = v3(cs, 0, -sn), ay = v3(0, 1, 0), az = v3(sn, 0, cs);
  V3 ex = ax * h.x, ey = ay * h.y, ez = az * h.z;
  quadFace(c + ey, ex, ez, ay, col, lit);
  quadFace(c + ex, ey, ez, ax, col, lit);
  quadFace(c - ex, ey, ez, ax * -1.0f, col, lit);
  quadFace(c + ez, ex, ey, az, col, lit);
  quadFace(c - ez, ex, ey, az * -1.0f, col, lit);
}
// Призма (колесо) вокруг оси X кузова
static void drawWheel(V3 c, V3 ax, V3 ay, V3 az, float r, float hw, u32 col) {
  // мелкие детали (фары, колпаки) рисуются с меньшим числом граней - на картинке не видно, а полигонов втрое меньше
  int N = r < 0.13f ? imin(wheelN, 8) : (r < 0.25f ? imin(wheelN, 12) : wheelN);
  static float CT[25][24], ST[25][24];
  static bool tabOk[25];
  if (N < 6) N = 6;
  if (!tabOk[N]) {
    for (int i = 0; i < N; i++) { float t = 6.2831853f * i / N; CT[N][i] = cosf(t); ST[N][i] = sinf(t); }
    tabOk[N] = true;
  }
  V3 ring[24];
  for (int i = 0; i < N; i++) ring[i] = ay * (r * CT[N][i]) + az * (r * ST[N][i]);
  for (int i = 0; i < N; i++) {
    int j = (i + 1) % N;
    V3 p[4];
    p[0] = c + ring[i] - ax * hw; p[1] = c + ring[j] - ax * hw;
    p[2] = c + ring[j] + ax * hw; p[3] = c + ring[i] + ax * hw;
    V3 mid = vnorm(ring[i] + ring[j]);
    faceN(p, 4, mid, col, true);
  }
  V3 cap[24];
  for (int s = -1; s <= 1; s += 2) {
    for (int i = 0; i < N; i++) cap[i] = c + ring[i] + ax * (hw * s);
    faceN(cap, N, ax * (float)s, col, true);
  }
}

static void setClipRect(int x0, int y0, int x1, int y1) { clipX0 = x0; clipY0 = y0; clipX1 = x1; clipY1 = y1; }
static void clearDepth() {
  if (clipX0 == 0 && clipX1 == IW) { memset(zb + clipY0 * IW, 0, sizeof(float) * (size_t)(clipY1 - clipY0) * IW); return; }
  for (int y = clipY0; y < clipY1; y++) { float *r = zb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = 0; }
}
static void fillSky() {
  float horizon = vcy + focal * camF.y / sqrtf(fmaxv(0.05f, 1 - camF.y * camF.y));
  u32 top = mixc(RGB(60, 70, 110), RGB(6, 8, 26), gNight);
  for (int y = clipY0; y < clipY1; y++) {
    u32 c = y < horizon ? mixc(top, fogCol, clampf((y - clipY0) / fmaxv(1.0f, horizon - clipY0), 0, 1)) : fogCol;
    u32 *r = fb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = c;
  }
}
static void groundQuad(float x0, float z0, float x1, float z1, float y, u32 col) {
  if (gLight < 0.999f) col = RGB((int)(CR(col) * gLight), (int)(CG(col) * gLight), (int)(CB(col) * gLight));
  V3 p[4] = {v3(x0, y, z0), v3(x1, y, z0), v3(x1, y, z1), v3(x0, y, z1)};
  V3 vv[4]; for (int i = 0; i < 4; i++) vv[i] = toView(p[i]);
  drawPolyView(vv, 4, col);
}
static void discOct(float cx, float cz, float r, float y, u32 col) {
  V3 vv[8];
  for (int i = 0; i < 8; i++) { float t = 6.2831853f * i / 8 + 0.3927f; vv[i] = toView(v3(cx + cosf(t) * r, y, cz + sinf(t) * r)); }
  drawPolyView(vv, 8, col);
}
static bool hitR(int x, int y, int w, int h) {
  if (!P.clicked) return false;
  if (P.cx >= x && P.cx < x + w && P.cy >= y && P.cy < y + h && P.sx >= x && P.sx < x + w && P.sy >= y && P.sy < y + h) { P.clicked = false; return true; }
  return false;
}
static bool button(int x, int y, int w, int h, const char *label, bool en, u32 col) {
  u32 c = en ? col : RGB(58, 60, 70);
  fillRect(x, y, w, h, shadec(c, 0.55f));
  fillRect(x + 1, y + 1, w - 2, h - 2, c);
  fillRect(x + 1, y + 1, w - 2, imax(1, h / 8), shadec(c, 1.25f));
  char lines[3][64]; int n = 0; const char *p = label;
  while (n < 3) {
    const char *e = strchr(p, '\n'); int len = e ? (int)(e - p) : (int)strlen(p); if (len > 63) len = 63;
    memcpy(lines[n], p, len); lines[n][len] = 0; n++; if (!e) break; p = e + 1;
  }
  int sc = fs; while (sc > 1 && textW(lines[0], sc) > w - 4) sc--;
  int lh = 9 * sc, y0 = y + (h - n * lh + 2 * sc) / 2;
  for (int i = 0; i < n; i++) drawText(x + w / 2, y0 + i * lh, lines[i], sc, en ? RGB(255, 255, 255) : RGB(140, 142, 152), 1);
  return en && hitR(x, y, w, h);
}
static void drawLine(int x0, int y0, int x1, int y1, u32 c, int th) {
  int dx = abs(x1 - x0), dy = abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy, n = 0;
  while (n++ < 4000) {
    fillRect(x0 - th / 2, y0 - th / 2, th, th, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err; if (e2 > -dy) { err -= dy; x0 += sx; } if (e2 < dx) { err += dx; y0 += sy; }
  }
}


// =====================================================================
//  СБОРКА ПК: детали, заказы, проверка сборки
// =====================================================================
#include <algorithm>
#include <string>
enum { P_CASE, P_MB, P_CPU, P_COOLER, P_RAM, P_GPU, P_PSU, P_SSD, NPT };
static const char *PTN[NPT] = {"КОРПУС", "ПЛАТА", "ПРОЦЕССОР", "КУЛЕР", "ПАМЯТЬ", "ВИДЕОКАРТА", "БЛОК ПИТАНИЯ", "ДИСК"};
// a,b,c,d - свои у каждого типа:
//  корпус: a размер (0 малый mATX, 1 большой ATX), b макс. длина видеокарты (мм), c цвет, d подсветка
//  плата: a сокет (0 "АМ5", 1 "ЛГА"), b тип памяти (4/5), c слотов памяти, d размер (0 mATX, 1 ATX)
//  процессор: a сокет, b мощность, c нагрев (Вт), d ядер
//  кулер: a отводит Вт, b подсветка
//  память: a тип (4/5), b ГБ
//  видеокарта: a мощность, b нагрев (Вт), c длина (мм)
//  блок питания: a Вт
//  диск: a ГБ, b скорость (0 HDD, 1 SSD, 2 NVMe)
struct Part { int type; const char *name; int price, tier, a, b, c, d; };
static const Part PARTS[] = {
  {P_CASE, "Корпус Бокс Мини", 2500, 1, 0, 260, 0, 0}, {P_CASE, "Корпус Тауэр", 4500, 1, 1, 330, 1, 0}, {P_CASE, "Корпус Стекло RGB", 8500, 2, 1, 380, 2, 1}, {P_CASE, "Корпус Титан XL", 14000, 3, 1, 420, 3, 1},
  {P_MB, "Плата А-320М (АМ5, DDR4)", 5000, 1, 0, 4, 2, 0}, {P_MB, "Плата Б-550 (АМ5, DDR5)", 9500, 2, 0, 5, 4, 1}, {P_MB, "Плата Х-670 (АМ5, DDR5)", 18000, 3, 0, 5, 4, 1},
  {P_MB, "Плата Н-610М (ЛГА, DDR4)", 5500, 1, 1, 4, 2, 0}, {P_MB, "Плата Б-760 (ЛГА, DDR5)", 10500, 2, 1, 5, 4, 1}, {P_MB, "Плата З-790 (ЛГА, DDR5)", 21000, 3, 1, 5, 4, 1},
  {P_CPU, "Аргон 3 (АМ5, 4 ядра)", 6000, 1, 0, 30, 65, 4}, {P_CPU, "Аргон 5 (АМ5, 6 ядер)", 13000, 2, 0, 55, 95, 6}, {P_CPU, "Аргон 7 (АМ5, 8 ядер)", 24000, 3, 0, 80, 125, 8}, {P_CPU, "Аргон 9 (АМ5, 16 ядер)", 46000, 4, 0, 110, 170, 16},
  {P_CPU, "Квант i3 (ЛГА, 4 ядра)", 6500, 1, 1, 32, 60, 4}, {P_CPU, "Квант i5 (ЛГА, 6 ядер)", 14000, 2, 1, 58, 110, 6}, {P_CPU, "Квант i7 (ЛГА, 12 ядер)", 27000, 3, 1, 85, 150, 12}, {P_CPU, "Квант i9 (ЛГА, 24 ядра)", 52000, 4, 1, 115, 220, 24},
  {P_COOLER, "Кулер Коробочный", 800, 1, 70, 0, 0, 0}, {P_COOLER, "Кулер Башня 120", 2500, 1, 130, 0, 0, 0}, {P_COOLER, "Кулер Башня RGB", 4500, 2, 170, 1, 0, 0}, {P_COOLER, "Водянка 360 RGB", 11000, 3, 250, 1, 0, 0},
  {P_RAM, "DDR4 8 ГБ", 1800, 1, 4, 8, 0, 0}, {P_RAM, "DDR4 16 ГБ", 3300, 1, 4, 16, 0, 0}, {P_RAM, "DDR5 16 ГБ", 4500, 2, 5, 16, 0, 0}, {P_RAM, "DDR5 32 ГБ", 8500, 3, 5, 32, 0, 0},
  {P_GPU, "Вектор 1650 (мини)", 13000, 1, 30, 75, 180, 0}, {P_GPU, "Вектор 3060", 28000, 2, 60, 170, 240, 0}, {P_GPU, "Радон 7700", 39000, 2, 75, 230, 270, 0},
  {P_GPU, "Вектор 4070", 58000, 3, 100, 220, 300, 0}, {P_GPU, "Радон 7900", 85000, 4, 130, 320, 340, 0}, {P_GPU, "Вектор 4090", 150000, 4, 170, 450, 360, 0},
  {P_PSU, "БП 400 Вт", 2500, 1, 400, 0, 0, 0}, {P_PSU, "БП 550 Вт", 4000, 1, 550, 0, 0, 0}, {P_PSU, "БП 750 Вт", 7000, 2, 750, 0, 0, 0}, {P_PSU, "БП 1000 Вт", 12000, 3, 1000, 0, 0, 0},
  {P_SSD, "HDD 1 ТБ", 3000, 1, 1000, 0, 0, 0}, {P_SSD, "SSD 256 ГБ", 2200, 1, 256, 1, 0, 0}, {P_SSD, "SSD 512 ГБ", 3500, 1, 512, 1, 0, 0}, {P_SSD, "NVMe 1 ТБ", 6500, 2, 1000, 2, 0, 0}, {P_SSD, "NVMe 2 ТБ", 12000, 3, 2000, 2, 0, 0},
};
static const int NPART = sizeof(PARTS) / sizeof(PARTS[0]);
static const char *SOCK[2] = {"АМ5", "ЛГА"};
// ---- заказы ----
enum { O_OFFICE, O_GAME, O_EDIT, O_STREAM, NOTYPE };
static const char *OTN[NOTYPE] = {"ОФИСНЫЙ", "ИГРОВОЙ", "ДЛЯ МОНТАЖА", "ДЛЯ СТРИМОВ"};
static const char *GAMES[] = {"ТАНКИ 5", "КИБЕРГОРОД", "ГОНКИ ХОРАЙЗН", "СТРЕЛЯЛКА 2", "КОСМОС ОНЛАЙН"};
static const char *CNAMES[] = {"Артём", "Света", "Дядя Коля", "Школьник Влад", "Блогер Ника", "Офис \"Ромашка\"", "Стример Макс", "Монтажёр Ира", "Бабушка Галя", "Геймер Тимур"};
struct Order { int type, budget, fps, cpu, ram, disk, game, client; bool rgb, active, done; };
static int cheapestPart(int type, int lvl, int a0, int a1, int need, int need2, int need3) {   // фильтр по полям для разных типов
  int best = -1;
  for (int i = 0; i < NPART; i++) {
    const Part &p = PARTS[i]; if (p.type != type || p.tier > lvl) continue;
    bool ok = true;
    if (type == P_MB) ok = p.a == a0;
    if (type == P_COOLER) ok = p.a >= need && (!need2 || p.b);
    if (type == P_CASE) ok = p.a >= a0 && p.b >= need && (!need2 || p.d || need3);
    if (type == P_PSU) ok = p.a >= need;
    if (type == P_SSD) ok = p.a >= need;
    if (ok && (best < 0 || p.price < PARTS[best].price)) best = i;
  }
  return best;
}
// ---- состояние ----
struct Build { int part[NPT]; int ram[4]; int nram; bool paste, cables, closed; };
static int money = 30000, rep = 0, day = 1, level = 1, paste = 3;
static std::vector<int> inv;                    // склад деталей (номера в PARTS)
static Order orders[3]; static int curOrder = -1;
static Build bd;
static bool tested = false; static int testFps = 0, testScore = 0; static std::vector<std::string> problems;
static char msg[128] = ""; static float msgT = 0;
static void say(const char *s) { snprintf(msg, sizeof msg, "%s", s); msgT = 3.0f; }
static int levelOf(int r) { return r < 5 ? 1 : r < 15 ? 2 : r < 35 ? 3 : 4; }
static void clearBuild() { for (int i = 0; i < NPT; i++) bd.part[i] = -1; bd.nram = 0; bd.paste = bd.cables = bd.closed = false; tested = false; problems.clear(); }
static int planCost(const Order &o, int lvl);
static void genOrder(Order &o) {
  o.type = rand() % NOTYPE; if (level == 1 && o.type == O_STREAM) o.type = O_GAME;
  o.client = rand() % 10; o.game = rand() % 5; o.rgb = rand() % 3 == 0 && level >= 2; o.active = true; o.done = false;
  int lv = imin(4, level + (rand() % 3 == 0 ? 1 : 0));
  switch (o.type) {
    case O_OFFICE: o.fps = 0; o.cpu = 25 + rand() % 10; o.ram = 8; o.disk = 256; o.budget = 32000 + rand() % 8 * 1000; o.rgb = false; break;
    case O_GAME:   o.fps = 40 + lv * 20 + rand() % 3 * 10; o.cpu = 0; o.ram = lv >= 3 ? 32 : 16; o.disk = 512; o.budget = 0; break;
    case O_EDIT:   o.fps = 0; o.cpu = 50 + lv * 15; o.ram = lv >= 3 ? 64 : 32; o.disk = 1000; o.budget = 0; break;
    default:       o.fps = 60 + lv * 15; o.cpu = 50 + lv * 10; o.ram = 32; o.disk = 1000; o.budget = 0; break;
  }
  // бюджет: самая дешёвая подходящая сборка + 15-40% (на работу); если собрать нельзя - ослабляем требования
  int cost = -1;
  for (int k = 0; k < 8; k++) { cost = planCost(o, imin(4, level + 1)); if (cost > 0) break; if (o.fps) o.fps = o.fps * 85 / 100; if (o.cpu) o.cpu = o.cpu * 85 / 100; if (o.ram > 16) o.ram /= 2; }
  if (cost <= 0) { o.type = O_OFFICE; o.fps = 0; o.cpu = 25; o.ram = 8; o.disk = 256; o.rgb = false; cost = planCost(o, 4); }
  o.budget = (int)(cost * (1.15f + (rand() % 26) / 100.0f)) / 500 * 500;
}
// ---- проверка сборки ----
static const Part *PT(int t) { return bd.part[t] >= 0 ? &PARTS[bd.part[t]] : NULL; }
static int ramGB() { int s = 0; for (int i = 0; i < bd.nram; i++) s += PARTS[bd.ram[i]].b; return s; }
static int buildCost() { int c = 0; for (int i = 0; i < NPT; i++) if (bd.part[i] >= 0 && i != P_RAM) c += PARTS[bd.part[i]].price; for (int i = 0; i < bd.nram; i++) c += PARTS[bd.ram[i]].price; return c; }
static int powerNeed() { int w = 60; if (PT(P_CPU)) w += PT(P_CPU)->c; if (PT(P_GPU)) w += PT(P_GPU)->b; return (int)(w * 1.25f); }
static float cpuScore() { if (!PT(P_CPU)) return 0; float s = (float)PT(P_CPU)->b; if (!bd.paste || !PT(P_COOLER) || PT(P_COOLER)->a < PT(P_CPU)->c) s *= 0.6f; if (ramGB() < 16) s *= 0.85f; return s; }
static int estFps(int game) {                    // FPS в игре: упирается в процессор или видеокарту
  static const float heavy[5] = {0.9f, 1.4f, 1.15f, 0.8f, 1.05f};
  float g = PT(P_GPU) ? PT(P_GPU)->a * 1.6f : cpuScore() * 0.2f, c = cpuScore() * 2.2f;
  return (int)(fminv(g, c) / heavy[game]);
}
static bool hasRgb() { return (PT(P_CASE) && PT(P_CASE)->d) || (PT(P_COOLER) && PT(P_COOLER)->b); }
static void checkBuild(std::vector<std::string> &pr, bool &boots) {
  pr.clear(); boots = true;
  const Part *cs = PT(P_CASE), *mb = PT(P_MB), *cpu = PT(P_CPU), *co = PT(P_COOLER), *gpu = PT(P_GPU), *psu = PT(P_PSU), *sd = PT(P_SSD);
  if (!cs || !mb || !cpu || !psu || !bd.nram) { std::string s = "НЕ ХВАТАЕТ:"; if (!cs) s += " КОРПУС"; if (!mb) s += " ПЛАТА"; if (!cpu) s += " ПРОЦЕССОР"; if (!bd.nram) s += " ПАМЯТЬ"; if (!psu) s += " БЛОК ПИТАНИЯ"; pr.push_back(s); boots = false; }
  if (cs && mb && mb->d > cs->a) { pr.push_back("ПЛАТА НЕ ВЛЕЗАЕТ В МАЛЫЙ КОРПУС"); boots = false; }
  if (mb && cpu && mb->a != cpu->a) { pr.push_back("ПРОЦЕССОР НЕ ПОДХОДИТ К СОКЕТУ ПЛАТЫ"); boots = false; }
  if (mb) for (int i = 0; i < bd.nram; i++) if (PARTS[bd.ram[i]].a != mb->b) { pr.push_back("ПАМЯТЬ НЕ ТОГО ТИПА (DDR4 / DDR5)"); boots = false; break; }
  if (cpu && !co) { pr.push_back("НЕТ КУЛЕРА: ПРОЦЕССОР ПЕРЕГРЕВАЕТСЯ"); boots = false; }
  if (cpu && co && !bd.paste) pr.push_back("ЗАБЫЛ ТЕРМОПАСТУ: ПЕРЕГРЕВ, ТОРМОЗИТ");
  if (cpu && co && co->a < cpu->c) pr.push_back("СЛАБЫЙ КУЛЕР: ПОД НАГРУЗКОЙ ТОРМОЗИТ");
  if (cs && gpu && gpu->c > cs->b) { pr.push_back("ВИДЕОКАРТА НЕ ВЛЕЗАЕТ В КОРПУС"); boots = false; }
  if (psu && psu->a < powerNeed()) { char b[96]; snprintf(b, sizeof b, "БЛОК ПИТАНИЯ СЛАБЫЙ: НУЖНО %d ВТ", powerNeed()); pr.push_back(b); boots = false; }
  if (!sd) { pr.push_back("НЕТ ДИСКА: НЕКУДА СТАВИТЬ СИСТЕМУ"); boots = false; }
  if (!bd.cables) { pr.push_back("НЕ ПОДКЛЮЧЕНЫ ПРОВОДА"); boots = false; }
}

// самая дешёвая сборка под заказ (перебор процессоров и видеокарт); -1, если на этом уровне не собрать
static int planCost(const Order &o, int lvl) {
  Build keep = bd; int best = -1;
  for (int c = 0; c < NPART; c++) {
    const Part &cp = PARTS[c]; if (cp.type != P_CPU || cp.tier > lvl) continue;
    for (int g = -1; g < NPART; g++) {
      if (g >= 0 && (PARTS[g].type != P_GPU || PARTS[g].tier > lvl)) continue;
      int mb = cheapestPart(P_MB, lvl, cp.a, 0, 0, 0, 0); if (mb < 0) continue; const Part &mp = PARTS[mb];
      int ram = -1, nr = 0;
      for (int r = 0; r < NPART; r++) { const Part &rp = PARTS[r]; if (rp.type != P_RAM || rp.tier > lvl || rp.a != mp.b) continue; int n = imax(1, (o.ram + rp.b - 1) / rp.b); if (n > mp.c) continue; if (ram < 0 || rp.price * n < PARTS[ram].price * nr) { ram = r; nr = n; } }
      if (ram < 0) continue;
      int co = cheapestPart(P_COOLER, lvl, 0, 0, cp.c, 0, 0); if (co < 0) continue;
      int cs = cheapestPart(P_CASE, lvl, mp.d, 0, g >= 0 ? PARTS[g].c : 0, o.rgb, PARTS[co].b); if (cs < 0) continue;
      int ps = cheapestPart(P_PSU, lvl, 0, 0, (int)((60 + cp.c + (g >= 0 ? PARTS[g].b : 0)) * 1.25f), 0, 0); if (ps < 0) continue;
      int sd = cheapestPart(P_SSD, lvl, 0, 0, o.disk, 0, 0); if (sd < 0) continue;
      for (int i = 0; i < NPT; i++) bd.part[i] = -1;
      bd.part[P_CASE] = cs; bd.part[P_MB] = mb; bd.part[P_CPU] = c; bd.part[P_COOLER] = co; bd.part[P_GPU] = g; bd.part[P_PSU] = ps; bd.part[P_SSD] = sd;
      bd.nram = nr; for (int i = 0; i < nr; i++) bd.ram[i] = ram; bd.paste = true; bd.cables = true;
      if (o.fps && estFps(o.game) < o.fps) continue;
      if (o.cpu && cpuScore() < o.cpu) continue;
      int cost = buildCost(); if (best < 0 || cost < best) best = cost;
    }
  }
  bd = keep;
  return best;
}

// ---------- сохранение ----------
static void saveGame() {
  FILE *f = fopen(pth("pcbuilder.sav"), "w"); if (!f) return;
  fprintf(f, "1 %d %d %d %d %d\n", money, rep, day, paste, curOrder);
  fprintf(f, "%d", (int)inv.size()); for (size_t i = 0; i < inv.size(); i++) fprintf(f, " %d", inv[i]); fprintf(f, "\n");
  for (int i = 0; i < 3; i++) { Order &o = orders[i]; fprintf(f, "%d %d %d %d %d %d %d %d %d %d\n", o.type, o.budget, o.fps, o.cpu, o.ram, o.disk, o.game, o.client, o.rgb, o.active); }
  for (int i = 0; i < NPT; i++) fprintf(f, "%d ", bd.part[i]); fprintf(f, "%d", bd.nram); for (int i = 0; i < 4; i++) fprintf(f, " %d", bd.ram[i]); fprintf(f, " %d %d %d\n", bd.paste, bd.cables, bd.closed);
  fclose(f);
}
static bool loadGame() {
  FILE *f = fopen(pth("pcbuilder.sav"), "r"); if (!f) return false;
  int ver = 0, n = 0; bool ok = fscanf(f, "%d %d %d %d %d %d", &ver, &money, &rep, &day, &paste, &curOrder) == 6;
  if (ok && fscanf(f, "%d", &n) == 1) { inv.clear(); for (int i = 0; i < n; i++) { int v; if (fscanf(f, "%d", &v) == 1 && v >= 0 && v < NPART) inv.push_back(v); } }
  for (int i = 0; i < 3 && ok; i++) { Order &o = orders[i]; int rgb = 0, act = 0; ok = fscanf(f, "%d %d %d %d %d %d %d %d %d %d", &o.type, &o.budget, &o.fps, &o.cpu, &o.ram, &o.disk, &o.game, &o.client, &rgb, &act) == 10; o.rgb = rgb; o.active = act; o.done = false; }
  if (ok) { for (int i = 0; i < NPT; i++) if (fscanf(f, "%d", &bd.part[i]) != 1) ok = false; int p1 = 0, p2 = 0, p3 = 0; if (fscanf(f, "%d %d %d %d %d %d %d %d", &bd.nram, &bd.ram[0], &bd.ram[1], &bd.ram[2], &bd.ram[3], &p1, &p2, &p3) == 8) { bd.paste = p1; bd.cables = p2; bd.closed = p3; } else ok = false; }
  fclose(f); level = levelOf(rep);
  return ok;
}
static void newGame() { money = 30000; rep = 0; day = 1; level = 1; paste = 3; inv.clear(); clearBuild(); curOrder = -1; for (int i = 0; i < 3; i++) genOrder(orders[i]); }

// ---------- установка деталей с анимацией ----------
struct Anim { int part; int slot; float t; };       // деталь летит со стола на место
static std::vector<Anim> anims;
static float powerT = 0; static bool powered = false;  // запуск при тесте: вентиляторы крутятся, подсветка
static bool installPart(int ii) {
  int pid = inv[ii]; const Part &p = PARTS[pid];
  if (curOrder < 0) { say("СНАЧАЛА ВОЗЬМИ ЗАКАЗ"); return false; }
  if (p.type != P_CASE && bd.part[P_CASE] < 0) { say("СНАЧАЛА ПОСТАВЬ КОРПУС"); return false; }
  if ((p.type == P_CPU || p.type == P_RAM || p.type == P_COOLER || p.type == P_GPU || p.type == P_SSD) && bd.part[P_MB] < 0) { say("СНАЧАЛА ПОСТАВЬ ПЛАТУ"); return false; }
  if (p.type == P_COOLER && bd.part[P_CPU] < 0) { say("СНАЧАЛА ПРОЦЕССОР"); return false; }
  if (p.type == P_RAM) {
    int slots = bd.part[P_MB] >= 0 ? PARTS[bd.part[P_MB]].c : 4;
    if (bd.nram >= slots) { say("ВСЕ СЛОТЫ ПАМЯТИ ЗАНЯТЫ"); return false; }
    bd.ram[bd.nram++] = pid;
  } else {
    if (bd.part[p.type] >= 0) { say("ЭТО МЕСТО УЖЕ ЗАНЯТО: СНАЧАЛА СНИМИ СТАРУЮ"); return false; }
    bd.part[p.type] = pid;
  }
  inv.erase(inv.begin() + ii);
  Anim a = {pid, p.type, 0}; anims.push_back(a);
  tested = false; bd.closed = false; if (p.type != P_PSU) bd.cables = bd.cables && p.type != P_GPU && p.type != P_SSD && p.type != P_MB;
  return true;
}
static void removePart(int t) {                  // снять деталь обратно на склад (снимает и всё, что на ней стоит)
  if (t == P_RAM) { if (bd.nram > 0) inv.push_back(bd.ram[--bd.nram]); }
  else if (bd.part[t] >= 0) {
    if (t == P_MB) { for (int k : {P_COOLER, P_CPU, P_GPU, P_SSD}) if (bd.part[k] >= 0) { inv.push_back(bd.part[k]); bd.part[k] = -1; } while (bd.nram) inv.push_back(bd.ram[--bd.nram]); bd.paste = false; }
    if (t == P_CASE) { for (int k = 1; k < NPT; k++) if (k != P_RAM && bd.part[k] >= 0) { inv.push_back(bd.part[k]); bd.part[k] = -1; } while (bd.nram) inv.push_back(bd.ram[--bd.nram]); bd.paste = false; }
    if (t == P_CPU) { if (bd.part[P_COOLER] >= 0) { inv.push_back(bd.part[P_COOLER]); bd.part[P_COOLER] = -1; } bd.paste = false; }
    if (t == P_COOLER) bd.paste = false;
    inv.push_back(bd.part[t]); bd.part[t] = -1;
  }
  bd.cables = false; bd.closed = false; tested = false;
}

// ---------- 3D: стол, корпус, детали ----------
static float camYaw = -2.0f, camPitch = 0.42f, camDist = 2.35f;
static float caseH() { const Part *c = PT(P_CASE); return c && c->a ? 0.95f : 0.75f; }
static float caseD() { const Part *c = PT(P_CASE); return c && c->a ? 0.45f : 0.38f; }
static u32 rgbCol(float t) { return RGB((int)(128 + 127 * sinf(t)), (int)(128 + 127 * sinf(t + 2.1f)), (int)(128 + 127 * sinf(t + 4.2f))); }
static u32 litRGB(float t) { return powered ? rgbCol(t) : RGB(70, 70, 80); }   // подсветка горит, когда ПК включён
// кольцо из отрезков: axis 0 - плоскость YZ (смотрит вдоль X), 1 - плоскость XZ (смотрит вверх/вниз)
static void ring(V3 c, float r, int axis, u32 col, float th) {
  for (int i = 0; i < 12; i++) {
    float a = i / 12.0f * 6.2832f, s = r * 0.27f;
    if (axis == 2) drawBox(c + v3(cosf(a) * r, sinf(a) * r, 0), v3(s * fabsf(sinf(a)) + th, s * fabsf(cosf(a)) + th, th), 0, col, false);
    else if (axis == 0) drawBox(c + v3(0, sinf(a) * r, cosf(a) * r), v3(th, s, th), 0, col, false);
    else drawBox(c + v3(sinf(a) * r, 0, cosf(a) * r), v3(s, th, th), -a, col, false);
  }
}
// вентилятор: рамка, кольцо подсветки, ступица и 7 лопастей
static void fan(V3 c, float r, int axis, bool rgb) {
  float sp = powered ? gTime * 22.0f : 0.4f; u32 fr = RGB(28, 28, 32), bl = RGB(55, 55, 62);
  if (axis == 2) {                                         // смотрит вдоль Z: передние и задний вентиляторы, кулер-башня
    drawBox(c + v3(0, r, 0), v3(r, 0.008f, 0.008f), 0, fr, true); drawBox(c - v3(0, r, 0), v3(r, 0.008f, 0.008f), 0, fr, true);
    drawBox(c + v3(r, 0, 0), v3(0.008f, r, 0.008f), 0, fr, true); drawBox(c - v3(r, 0, 0), v3(0.008f, r, 0.008f), 0, fr, true);
    if (rgb) ring(c, r * 0.9f, 2, litRGB(gTime * 2), 0.004f);
    drawBox(c, v3(r * 0.22f, r * 0.22f, 0.006f), 0, RGB(35, 35, 40), true);
    for (int k = 0; k < 7; k++) { float a = sp + k * 0.8976f; drawBox(c + v3(cosf(a) * r * 0.55f, sinf(a) * r * 0.55f, 0), v3(r * 0.13f + fabsf(sinf(a)) * r * 0.2f, r * 0.13f + fabsf(cosf(a)) * r * 0.2f, 0.002f), 0, bl, false); }
    return;
  }
  if (axis == 0) {
    drawBox(c + v3(0, r, 0), v3(0.008f, 0.008f, r), 0, fr, true); drawBox(c - v3(0, r, 0), v3(0.008f, 0.008f, r), 0, fr, true);
    drawBox(c + v3(0, 0, r), v3(0.008f, r, 0.008f), 0, fr, true); drawBox(c - v3(0, 0, r), v3(0.008f, r, 0.008f), 0, fr, true);
    if (rgb) ring(c - v3(0.006f, 0, 0), r * 0.9f, 0, litRGB(gTime * 2), 0.004f);
    drawBox(c - v3(0.004f, 0, 0), v3(0.006f, r * 0.22f, r * 0.22f), 0, RGB(35, 35, 40), true);
    for (int k = 0; k < 7; k++) { float a = sp + k * 0.8976f; drawBox(c + v3(-0.004f, sinf(a) * r * 0.55f, cosf(a) * r * 0.55f), v3(0.002f, r * 0.13f + fabsf(cosf(a)) * r * 0.2f, r * 0.13f + fabsf(sinf(a)) * r * 0.2f), 0, bl, false); }
  } else {
    drawBox(c + v3(r, 0, 0), v3(0.008f, 0.008f, r), 0, fr, true); drawBox(c - v3(r, 0, 0), v3(0.008f, 0.008f, r), 0, fr, true);
    drawBox(c + v3(0, 0, r), v3(r, 0.008f, 0.008f), 0, fr, true); drawBox(c - v3(0, 0, r), v3(r, 0.008f, 0.008f), 0, fr, true);
    if (rgb) ring(c - v3(0, 0.006f, 0), r * 0.9f, 1, litRGB(gTime * 2), 0.004f);
    drawBox(c - v3(0, 0.004f, 0), v3(r * 0.22f, 0.006f, r * 0.22f), 0, RGB(35, 35, 40), true);
    for (int k = 0; k < 7; k++) { float a = sp + k * 0.8976f; drawBox(c + v3(sinf(a) * r * 0.55f, -0.004f, cosf(a) * r * 0.55f), v3(r * 0.32f, 0.002f, r * 0.12f), a, bl, false); }
  }
}
static V3 slotPos(int t, int idx) {
  float H = caseH(), D = caseD();
  switch (t) {
    case P_CASE: return v3(0, H * 0.5f, 0);
    case P_MB: return v3(0.195f, H * 0.5f + 0.06f, -D * 0.25f);
    case P_CPU: return v3(0.188f, H - 0.24f, -0.1f);
    case P_COOLER: return v3(0.12f, H - 0.24f, -0.1f);
    case P_RAM: return v3(0.175f, H - 0.24f, 0.01f + idx * 0.022f);
    case P_GPU: return v3(0.13f, H * 0.42f, -D + 0.05f + 0.15f);
    case P_PSU: return v3(0.0f, 0.085f, D - 0.13f);
    default: return v3(0.188f, H * 0.32f, -0.02f);
  }
}
static void drawPart(int pid, V3 c, float k) {
  const Part &p = PARTS[pid]; float H = caseH(), D = caseD(); (void)k;
  switch (p.type) {
    case P_CASE: break;
    case P_MB: {                                                            // материнская плата: текстолит, радиаторы, слоты, разъёмы
      float hh = H * 0.38f, hz = D * 0.6f; u32 pcb = p.b == 5 ? RGB(24, 28, 40) : RGB(22, 54, 34);
      drawBox(c, v3(0.004f, hh, hz), 0, pcb, true);
      for (int i = 0; i < 7; i++) drawBox(c + v3(-0.005f, -hh + 0.05f + i * hh * 0.28f, -hz * 0.2f), v3(0.0005f, 0.002f, hz * 0.6f), 0, mixc(pcb, RGB(200, 180, 90), 0.25f), false);   // дорожки
      V3 sock = slotPos(P_CPU, 0);
      drawBox(v3(c.x - 0.006f, sock.y, sock.z), v3(0.003f, 0.045f, 0.045f), 0, RGB(150, 150, 160), true);                   // рамка сокета
      drawBox(v3(c.x - 0.012f, sock.y + 0.075f, sock.z - 0.01f), v3(0.01f, 0.018f, 0.06f), 0, RGB(70, 72, 80), true);       // радиаторы питания
      drawBox(v3(c.x - 0.012f, sock.y, sock.z - 0.075f), v3(0.01f, 0.06f, 0.016f), 0, RGB(70, 72, 80), true);
      drawBox(v3(c.x - 0.013f, sock.y + 0.05f, sock.z - 0.115f), v3(0.012f, 0.05f, 0.02f), 0, RGB(40, 42, 48), true);       // кожух разъёмов
      drawBox(v3(c.x - 0.026f, sock.y + 0.05f, sock.z - 0.135f), v3(0.001f, 0.045f, 0.003f), 0, litRGB(gTime * 2 + 1), false);   // подсветка на кожухе
      for (int i = 0; i < p.c; i++) drawBox(v3(c.x - 0.006f, H - 0.24f, 0.01f + i * 0.022f), v3(0.004f, 0.068f, 0.003f), 0, RGB(30, 30, 34), true);   // слоты памяти
      for (int i = 0; i < 3; i++) drawBox(v3(c.x - 0.006f, H * 0.42f - 0.035f - i * 0.065f, -D + 0.25f), v3(0.004f, 0.004f, 0.09f), 0, i ? RGB(30, 30, 34) : RGB(170, 170, 180), true);   // слоты PCIe
      drawBox(v3(c.x - 0.01f, H * 0.32f - 0.05f, 0.02f), v3(0.008f, 0.025f, 0.03f), 0, RGB(60, 62, 70), true);              // радиатор чипсета
      drawBox(v3(c.x - 0.006f, H - 0.24f, 0.11f), v3(0.006f, 0.04f, 0.008f), 0, RGB(220, 220, 225), true);                  // 24-пин
      for (int i = 0; i < 5; i++) drawBox(v3(c.x - 0.009f, sock.y + 0.035f - i * 0.015f, sock.z + 0.06f), v3(0.006f, 0.005f, 0.005f), 0, RGB(40, 40, 46), true);   // конденсаторы
    } break;
    case P_CPU:
      drawBox(c, v3(0.004f, 0.032f, 0.032f), 0, RGB(40, 90, 50), true);                                     // подложка
      drawBox(c - v3(0.004f, 0, 0), v3(0.002f, 0.026f, 0.026f), 0, RGB(205, 205, 212), true);               // крышка
      if (bd.paste && k >= 1) drawBox(c - v3(0.007f, 0, 0), v3(0.001f, 0.014f, 0.014f), 0, RGB(185, 185, 190), false);
      break;
    case P_COOLER:
      if (p.a >= 250) {                                                    // водянка: помпа с подсветкой, шланги, радиатор с тремя вентиляторами
        drawBox(c + v3(0.045f, 0, 0), v3(0.022f, 0.032f, 0.032f), 0, RGB(30, 30, 34), true);
        ring(c + v3(0.02f, 0, 0), 0.024f, 0, litRGB(gTime * 2), 0.004f);
        for (int i = -1; i <= 1; i += 2) drawBox(v3(0.03f, (c.y + H - 0.05f) * 0.5f, c.z + i * 0.02f), v3(0.008f, (H - 0.05f - c.y) * 0.5f, 0.008f), 0, RGB(25, 25, 28), true);
        drawBox(v3(0.0f, H - 0.035f, -0.05f), v3(0.14f, 0.015f, 0.21f), 0, RGB(30, 30, 34), true);
        for (int i = -1; i <= 1; i++) fan(v3(0.0f, H - 0.062f, -0.05f + i * 0.135f), 0.06f, 1, true);
      } else {
        float s = p.a >= 130 ? 1.0f : 0.6f;
        if (p.a < 130) { drawBox(c + v3(-0.02f, 0, 0), v3(0.02f, 0.045f, 0.045f), 0, RGB(150, 150, 158), true); fan(c + v3(-0.045f, 0, 0), 0.04f, 0, false); }   // коробочный
        else {                                                             // башня: медные трубки, рёбра, вентилятор
          for (int i = -1; i <= 1; i++) drawBox(c + v3(0.04f, 0.02f, i * 0.02f), v3(0.006f, 0.05f, 0.005f), 0, RGB(200, 120, 70), true);
          for (int i = 0; i < 12; i++) drawBox(c + v3(-0.01f, -0.06f + i * 0.012f, 0), v3(0.05f, 0.0025f, 0.06f), 0, RGB(190, 192, 200), true);
          drawBox(c + v3(-0.01f, 0.085f, 0), v3(0.052f, 0.006f, 0.062f), 0, p.b ? RGB(30, 30, 34) : RGB(170, 172, 180), true);
          if (p.b) drawBox(c + v3(-0.01f, 0.092f, 0), v3(0.04f, 0.002f, 0.05f), 0, litRGB(gTime * 2), false);
          fan(c + v3(-0.01f, 0, -0.07f * s), 0.06f * s, 2, p.b);
        }
      }
      break;
    case P_RAM:
      drawBox(c, v3(0.016f, 0.066f, 0.004f), 0, p.a == 5 ? RGB(38, 38, 46) : RGB(26, 80, 44), true);
      for (int i = 0; i < 4; i++) drawBox(c + v3(-0.0165f, -0.045f + i * 0.03f, 0), v3(0.001f, 0.008f, 0.0045f), 0, p.a == 5 ? RGB(80, 80, 92) : RGB(60, 120, 80), false);   // рёбра радиатора
      if (p.a == 5) drawBox(c + v3(-0.017f, 0, 0) + v3(0.0f, 0.0f, 0.0f) + v3(0, 0.0f, 0), v3(0.002f, 0.06f, 0.0015f), 0, litRGB(gTime * 3 + c.z * 40), false);   // RGB-полоска
      break;
    case P_GPU: {                                                           // видеокарта: кожух с гранями, бэкплейт, вентиляторы, логотип
      float L = p.c / 1000.0f, z0 = c.z - 0.15f; u32 sh = p.a >= 100 ? RGB(40, 40, 46) : RGB(30, 32, 38);
      drawBox(v3(c.x, c.y, z0 + L * 0.5f), v3(0.065f, 0.026f, L * 0.5f), 0, sh, true);
      drawBox(v3(c.x, c.y + 0.028f, z0 + L * 0.5f), v3(0.064f, 0.003f, L * 0.5f), 0, RGB(70, 72, 80), true);   // бэкплейт
      drawBox(v3(c.x - 0.066f, c.y, z0 + L * 0.5f), v3(0.002f, 0.024f, L * 0.48f), 0, RGB(90, 92, 100), true); // торец
      drawBox(v3(c.x - 0.068f, c.y + 0.005f, z0 + L * 0.3f), v3(0.001f, 0.006f, L * 0.18f), 0, litRGB(gTime * 2), false);   // светящийся логотип
      int nf = p.c >= 270 ? 3 : (p.c >= 220 ? 2 : 1); float fr = fminv(0.048f, L / nf * 0.42f);
      for (int i = 0; i < nf; i++) fan(v3(c.x, c.y - 0.03f, z0 + L * (i + 0.5f) / nf), fr, 1, p.a >= 100);
      drawBox(v3(c.x + 0.06f, c.y, z0 - 0.004f), v3(0.006f, 0.034f, 0.004f), 0, RGB(185, 185, 195), true);   // планка с выходами
      if (p.b >= 170) drawBox(v3(c.x - 0.02f, c.y + 0.034f, z0 + L * 0.75f), v3(0.012f, 0.006f, 0.01f), 0, RGB(30, 30, 30), true);   // разъём питания
    } break;
    case P_PSU:
      drawBox(c, v3(0.15f, 0.07f, 0.09f), 0, RGB(30, 30, 34), true);
      for (int i = 0; i < 6; i++) drawBox(c + v3(-0.151f, -0.04f + i * 0.016f, 0), v3(0.001f, 0.003f, 0.07f), 0, RGB(60, 60, 66), false);   // решётка
      drawBox(c + v3(-0.152f, 0.045f, 0.0f), v3(0.001f, 0.012f, 0.04f), 0, RGB(220, 180, 40), false);          // наклейка
      fan(c + v3(0, -0.072f, 0), 0.055f, 1, false);
      break;
    case P_SSD:
      if (p.b == 2) { drawBox(c, v3(0.004f, 0.012f, 0.04f), 0, RGB(25, 25, 28), true); drawBox(c - v3(0.005f, 0, 0), v3(0.002f, 0.013f, 0.038f), 0, RGB(80, 82, 90), true); }   // NVMe под радиатором
      else if (p.b == 1) { drawBox(c + v3(-0.05f, -0.12f, -0.1f), v3(0.035f, 0.005f, 0.05f), 0, RGB(40, 40, 46), true); drawBox(c + v3(-0.05f, -0.114f, -0.1f), v3(0.025f, 0.001f, 0.03f), 0, RGB(220, 60, 60), false); }
      else { drawBox(v3(-0.05f, 0.06f, -D + 0.1f), v3(0.05f, 0.015f, 0.07f), 0, RGB(150, 152, 160), true); drawBox(v3(-0.05f, 0.076f, -D + 0.1f), v3(0.035f, 0.001f, 0.04f), 0, RGB(240, 240, 240), false); }
      break;
  }
}
static void drawScene() {
  // комната: пол, стена, стол с ковриком, клавиатурой, мышкой, лампой
  groundQuad(-4, -4, 4, 4, -0.76f, RGB(110, 92, 80));
  drawBox(v3(1.6f, 0.4f, 0.0f), v3(0.02f, 1.2f, 2.0f), 0, RGB(70, 74, 90), true);
  drawBox(v3(1.57f, 0.55f, 0.0f), v3(0.004f, 0.01f, 1.9f), 0, litRGB(gTime * 0.5f), false);           // LED-лента на стене
  drawBox(v3(0, -0.04f, 0), v3(1.1f, 0.04f, 0.75f), 0, RGB(52, 44, 40), true);
  drawBox(v3(0, -0.002f, 0), v3(1.1f, 0.002f, 0.75f), 0, RGB(64, 54, 48), false);
  for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) drawBox(v3(i * 1.0f, -0.42f, j * 0.65f), v3(0.04f, 0.36f, 0.04f), 0, RGB(30, 30, 34), true);
  drawBox(v3(0.15f, 0.002f, 0.3f), v3(0.28f, 0.002f, 0.4f), 0, RGB(25, 25, 30), true);                  // коврик
  drawBox(v3(0.15f, 0.012f, 0.32f), v3(0.08f, 0.008f, 0.26f), 0, RGB(35, 35, 40), true);                // клавиатура
  for (int r = 0; r < 4; r++) drawBox(v3(0.1f + r * 0.03f, 0.021f, 0.32f), v3(0.01f, 0.002f, 0.24f), 0, litRGB(gTime * 1.5f + r), false);
  drawBox(v3(0.2f, 0.012f, 0.65f), v3(0.035f, 0.01f, 0.022f), 0.2f, RGB(30, 30, 34), true);              // мышь
  // монитор на подставке
  drawBox(v3(0.55f, 0.008f, 0.35f), v3(0.1f, 0.008f, 0.08f), 0, RGB(40, 40, 46), true);
  drawBox(v3(0.6f, 0.13f, 0.35f), v3(0.015f, 0.12f, 0.02f), 0, RGB(40, 40, 46), true);
  drawBox(v3(0.56f, 0.34f, 0.35f), v3(0.012f, 0.2f, 0.34f), 0, RGB(20, 20, 24), true);
  bool scrOn = powered && powerT > 1.0f;
  drawBox(v3(0.546f, 0.34f, 0.35f), v3(0.002f, 0.185f, 0.32f), 0, scrOn ? RGB(40, 110, 200) : RGB(8, 10, 14), false);
  if (scrOn) for (int i = 0; i < 4; i++) drawBox(v3(0.544f, 0.42f - i * 0.05f, 0.25f + (i % 2) * 0.05f), v3(0.001f, 0.012f, 0.12f + i * 0.03f), 0, RGB(200, 230, 255), false);
  // коробки со склада
  for (int i = 0; i < imin(8, (int)inv.size()); i++) { V3 bp = v3(-0.75f + (i % 4) * 0.13f, 0.03f + (i / 4) * 0.06f, -0.55f);
    drawBox(bp, v3(0.055f, 0.03f, 0.05f), i * 0.3f, RGB(175, 135, 85), true); drawBox(bp + v3(0, 0.031f, 0), v3(0.056f, 0.001f, 0.012f), i * 0.3f, RGB(200, 170, 120), false); }
  // корпус
  const Part *cs = PT(P_CASE);
  bool flying = false; for (size_t i = 0; i < anims.size(); i++) if (anims[i].slot == P_CASE) flying = true;
  if (cs && !flying) {
    float H = caseH(), D = caseD(); bool white = cs->c == 2;
    u32 cc = cs->c == 0 ? RGB(56, 58, 66) : cs->c == 1 ? RGB(26, 26, 30) : cs->c == 2 ? RGB(232, 232, 238) : RGB(16, 16, 20);
    u32 in = white ? RGB(215, 215, 222) : mixc(cc, RGB(0, 0, 0), 0.25f);
    for (int i = -1; i <= 1; i += 2) for (int j = -1; j <= 1; j += 2) drawBox(v3(i * 0.17f, -0.006f, j * (D - 0.05f)), v3(0.03f, 0.006f, 0.03f), 0, RGB(20, 20, 22), true);   // ножки
    drawBox(v3(0.217f, H * 0.5f, 0), v3(0.006f, H * 0.5f, D), 0, in, true);                       // стенка под плату
    for (int i = 0; i < 3; i++) drawBox(v3(0.21f, H * (0.3f + i * 0.2f), D * 0.55f), v3(0.001f, 0.035f, 0.02f), 0, RGB(15, 15, 18), false);   // отверстия для проводов
    drawBox(v3(0, H - 0.008f, 0), v3(0.222f, 0.008f, D), 0, cc, true);                            // верх с сеткой
    for (int i = 0; i < 6; i++) drawBox(v3(0, H + 0.0005f, -D * 0.6f + i * D * 0.24f), v3(0.18f, 0.001f, 0.012f), 0, mixc(cc, RGB(0, 0, 0), 0.4f), false);
    drawBox(v3(0, 0.008f, 0), v3(0.222f, 0.008f, D), 0, cc, true);
    drawBox(v3(0, H * 0.5f, -D - 0.012f), v3(0.222f, H * 0.5f, 0.016f), 0, cc, true);             // перед
    for (int i = 0; i < 9; i++) drawBox(v3(0, 0.12f + i * (H - 0.2f) / 9, -D - 0.029f), v3(0.15f, 0.004f, 0.001f), 0, mixc(cc, RGB(0, 0, 0), 0.35f), false);   // сетка спереди
    drawBox(v3(-0.18f, H - 0.06f, -D - 0.03f), v3(0.012f, 0.012f, 0.002f), 0, powered ? RGB(80, 200, 255) : RGB(90, 90, 100), false);   // кнопка включения
    if (cs->d) { drawBox(v3(0.0f, 0.05f, -D - 0.03f), v3(0.2f, 0.003f, 0.001f), 0, litRGB(gTime * 2), false); for (int i = 0; i < 2; i++) fan(v3(0.0f, H * (0.35f + i * 0.3f), -D + 0.03f), 0.07f, 2, true); }
    drawBox(v3(0, H * 0.5f, D), v3(0.222f, H * 0.5f, 0.008f), 0, cc, true);                        // зад
    for (int i = 0; i < 4; i++) drawBox(v3(0.12f, H * 0.42f - 0.035f - i * 0.065f, D + 0.009f), v3(0.006f, 0.02f, 0.001f), 0, RGB(150, 150, 160), false);   // заглушки слотов
    fan(v3(0.05f, H * 0.68f, D - 0.03f), 0.06f, 2, cs->d);
    // кожух блока питания (подвал)
    if (cs->a) { drawBox(v3(0, 0.165f, D * 0.25f), v3(0.215f, 0.004f, D * 0.75f), 0, in, true); drawBox(v3(-0.2f, 0.09f, D * 0.25f), v3(0.004f, 0.075f, D * 0.75f), 0, in, true);
      drawBox(v3(-0.205f, 0.09f, D * 0.25f), v3(0.001f, 0.02f, 0.08f), 0, litRGB(gTime * 2 + 3), false); }
    if (bd.closed) {                                                                               // боковая крышка: у стеклянного - тонированное стекло и рамка
      if (cs->d) { for (int i = -1; i <= 1; i += 2) { drawBox(v3(-0.224f, H * 0.5f, i * (D - 0.008f)), v3(0.004f, H * 0.5f, 0.01f), 0, cc, true); drawBox(v3(-0.224f, i > 0 ? H - 0.008f : 0.008f, 0), v3(0.004f, 0.008f, D), 0, cc, true); }
        for (int i = 0; i < 3; i++) drawBox(v3(-0.226f, H * (0.3f + i * 0.25f), -D * 0.3f + i * 0.1f), v3(0.0005f, 0.004f, D * 0.25f), -0.6f, RGB(150, 160, 175), false); }   // блики на стекле
      else drawBox(v3(-0.224f, H * 0.5f, 0), v3(0.004f, H * 0.5f, D), 0, cc, true);
    }
    for (int t = P_MB; t < NPT; t++) {
      if (t == P_RAM) { for (int i = 0; i < bd.nram; i++) { bool fl = false; for (auto &a : anims) if (a.slot == P_RAM && a.part == bd.ram[i] && i == bd.nram - 1) fl = true; if (!fl) drawPart(bd.ram[i], slotPos(P_RAM, i), 1); } continue; }
      if (bd.part[t] < 0) continue;
      bool fl = false; for (auto &a : anims) if (a.slot == t) fl = true;
      if (!fl) drawPart(bd.part[t], slotPos(t, 0), 1);
    }
    if (bd.cables) {                                                                               // аккуратные провода в оплётке
      u32 cw = white ? RGB(235, 235, 240) : RGB(28, 28, 32);
      V3 pin24 = v3(0.2f, H - 0.24f, 0.11f);
      drawBox(v3(0.205f, (pin24.y + 0.17f) * 0.5f, D * 0.55f), v3(0.006f, (pin24.y - 0.17f) * 0.5f, 0.02f), 0, cw, false);
      drawBox(v3(0.2f, pin24.y, (pin24.z + D * 0.55f) * 0.5f), v3(0.008f, 0.02f, (D * 0.55f - pin24.z) * 0.5f), 0, cw, false);
      drawBox(v3(0.205f, H - 0.06f, -0.15f), v3(0.006f, 0.008f, 0.05f), 0, cw, false);            // питание процессора сверху
      if (bd.part[P_GPU] >= 0 && PARTS[bd.part[P_GPU]].b >= 170) drawBox(v3(0.11f, (H * 0.42f + 0.17f) * 0.5f + 0.02f, -D + 0.05f + 0.15f + PARTS[bd.part[P_GPU]].c / 1000.0f * 0.75f - 0.15f), v3(0.006f, (H * 0.42f - 0.17f) * 0.5f, 0.006f), 0, cw, false);
    }
  }
  // летящие детали
  for (size_t i = 0; i < anims.size(); i++) {
    const Anim &a = anims[i]; float kk = fminv(1.0f, a.t / 0.6f), e = kk * kk * (3 - 2 * kk);
    V3 from = v3(-0.7f, 0.15f, -0.55f), to = slotPos(a.slot, a.slot == P_RAM ? bd.nram - 1 : 0);
    V3 pnt = from + (to - from) * e + v3(0, sinf(e * 3.14159f) * 0.25f, 0);
    if (a.slot == P_CASE) { float H = caseH(), D = caseD(); drawBox(pnt, v3(0.22f, H * 0.5f, D), e * 6.28f, RGB(56, 58, 66), true); }
    else drawPart(a.part, pnt, kk);
  }
}
static bool scr(V3 p, int &x, int &y) { V3 v = toView(p); if (v.z < 0.05f) return false; x = (int)(vcx + v.x * focal / v.z); y = (int)(vcy - v.y * focal / v.z); return true; }

// ---------- интерфейс ----------
static int tab = 0, shopCat = P_CPU, page = 0;
static float testT = -1; static bool testBoots = false;
static void fmtR(char *b, int n, int v) { char t[32]; snprintf(t, sizeof t, "%d", v < 0 ? -v : v); int L = (int)strlen(t), k = 0; char o[48]; if (v < 0) o[k++] = '-'; for (int i = 0; i < L; i++) { o[k++] = t[i]; if ((L - i - 1) % 3 == 0 && i < L - 1) o[k++] = ' '; } o[k] = 0; snprintf(b, n, "%s Р", o); }
struct ReqLine { char text[96]; bool ok; };
static int orderReqs(const Order &o, ReqLine *r) {
  int n = 0; char v[32];
  if (o.fps) { snprintf(r[n].text, 96, "FPS В \"%s\" ОТ %d (СЕЙЧАС ~%d)", GAMES[o.game], o.fps, estFps(o.game)); r[n].ok = estFps(o.game) >= o.fps; n++; }
  if (o.cpu) { snprintf(r[n].text, 96, "МОЩНОСТЬ ПРОЦЕССОРА ОТ %d (СЕЙЧАС %d)", o.cpu, (int)cpuScore()); r[n].ok = cpuScore() >= o.cpu; n++; }
  snprintf(r[n].text, 96, "ПАМЯТЬ ОТ %d ГБ (СЕЙЧАС %d)", o.ram, ramGB()); r[n].ok = ramGB() >= o.ram; n++;
  snprintf(r[n].text, 96, "ДИСК ОТ %d ГБ (СЕЙЧАС %d)", o.disk, PT(P_SSD) ? PT(P_SSD)->a : 0); r[n].ok = PT(P_SSD) && PT(P_SSD)->a >= o.disk; n++;
  if (o.rgb) { snprintf(r[n].text, 96, "ХОЧЕТ ПОДСВЕТКУ RGB"); r[n].ok = hasRgb(); n++; }
  fmtR(v, sizeof v, buildCost()); char bb[32]; fmtR(bb, sizeof bb, o.budget);
  snprintf(r[n].text, 96, "ДЕТАЛИ: %s ИЗ БЮДЖЕТА %s", v, bb); r[n].ok = buildCost() <= o.budget; n++;
  return n;
}
static void deliver() {
  if (curOrder < 0) return;
  Order &o = orders[curOrder]; ReqLine r[8]; int n = orderReqs(o, r); int miss = 0; for (int i = 0; i < n - 1; i++) if (!r[i].ok) miss++;
  int cost = buildCost(); char b[128], m[32];
  if (miss == 0) {
    int tip = (o.fps && estFps(o.game) >= o.fps * 13 / 10) ? o.budget / 20 : 0;
    money += o.budget + tip; rep += (cost <= o.budget * 85 / 100) ? 2 : 1;
    fmtR(m, sizeof m, o.budget - cost + tip); snprintf(b, sizeof b, "%s ДОВОЛЕН! ПРИБЫЛЬ %s%s", CNAMES[o.client], m, tip ? " (С ЧАЕВЫМИ)" : "");
  } else {
    int pay = o.budget * 6 / 10; money += pay; rep = imax(0, rep - 1);
    fmtR(m, sizeof m, pay - cost); snprintf(b, sizeof b, "НЕ ВСЁ КАК ПРОСИЛИ: ЗАПЛАТИЛИ 60%%, ИТОГ %s", m);
  }
  say(b);
  int oldL = level; level = levelOf(rep); if (level > oldL) { char lb[96]; snprintf(lb, sizeof lb, "НОВЫЙ УРОВЕНЬ %d: В МАГАЗИНЕ НОВЫЕ ДЕТАЛИ!", level); say(lb); }
  clearBuild(); anims.clear(); powered = false; genOrder(orders[curOrder]); curOrder = -1; day++; saveGame();
}
static void drawPanel() {
  char b[128], m[32];
  int px = IW * 62 / 100, pw = IW - px - fs, py = fs * 14, rowH = imax(fs * 13, (IH - py - fs * 30) / 8);
  fillRectA(px - fs, py - fs, pw + fs * 2, IH - py + fs, RGB(18, 22, 30), 225);
  int tw = (pw - fs * 2) / 3; const char *TN[3] = {"ЗАКАЗЫ", "МАГАЗИН", "СКЛАД"};
  for (int i = 0; i < 3; i++) { if (i == 2) snprintf(b, sizeof b, "СКЛАД (%d)", (int)inv.size()); else snprintf(b, sizeof b, "%s", TN[i]); if (button(px + i * (tw + fs), py, tw, fs * 12, b, true, tab == i ? RGB(70, 110, 170) : RGB(50, 56, 70))) { tab = i; page = 0; } }
  int y = py + fs * 15;
  if (tab == 0) {
    for (int i = 0; i < 3; i++) {
      Order &o = orders[i]; int h = (IH - y - fs * 4) / 3 - fs;
      fillRect(px, y, pw, h, i == curOrder ? RGB(40, 60, 40) : RGB(34, 38, 50));
      snprintf(b, sizeof b, "%s: %s ПК", CNAMES[o.client], OTN[o.type]); drawText(px + fs * 3, y + fs * 3, b, fs, RGB(255, 230, 160), 0);
      fmtR(m, sizeof m, o.budget); snprintf(b, sizeof b, "БЮДЖЕТ %s", m); drawText(px + fs * 3, y + fs * 12, b, fs, RGB(130, 230, 130), 0);
      std::string req; if (o.fps) { snprintf(b, sizeof b, "%d FPS В %s, ", o.fps, GAMES[o.game]); req += b; } if (o.cpu) { snprintf(b, sizeof b, "ПРОЦ. %d+, ", o.cpu); req += b; }
      snprintf(b, sizeof b, "%d ГБ, ДИСК %d", o.ram, o.disk); req += b; if (o.rgb) req += ", RGB";
      int sc = fs; while (sc > 1 && textW(req.c_str(), sc) > pw - fs * 6) sc--; drawText(px + fs * 3, y + fs * 21, req.c_str(), sc, RGB(200, 205, 220), 0);
      if (i == curOrder) drawText(px + pw - fs * 3, y + fs * 3, "В РАБОТЕ", fs, RGB(140, 240, 140), 2);
      else if (curOrder < 0 && button(px + pw - fs * 34, y + h - fs * 13, fs * 31, fs * 11, "ВЗЯТЬ", true, RGB(60, 150, 70))) { curOrder = i; clearBuild(); say("ЗАКАЗ ВЗЯТ: КУПИ ДЕТАЛИ В МАГАЗИНЕ И СОБЕРИ"); saveGame(); }
      y += h + fs;
    }
  } else if (tab == 1) {
    int cw = (pw - fs * 3) / 4;
    for (int t = 0; t < NPT; t++) if (button(px + (t % 4) * (cw + fs), y + (t / 4) * (fs * 11), cw, fs * 10, PTN[t], true, shopCat == t ? RGB(70, 110, 170) : RGB(50, 56, 70))) { shopCat = t; page = 0; }
    y += fs * 23;
    std::vector<int> list; for (int i = 0; i < NPART; i++) if (PARTS[i].type == shopCat) list.push_back(i);
    for (size_t k = 0; k < list.size(); k++) {
      const Part &p = PARTS[list[k]]; int ry = y + (int)k * (rowH + fs / 2); if (ry + rowH > IH - fs) break;
      bool locked = p.tier > level;
      fillRect(px, ry, pw, rowH, RGB(34, 38, 50));
      int sc = fs; while (sc > 1 && textW(p.name, sc) > pw - fs * 40) sc--; drawText(px + fs * 2, ry + rowH / 2 - fs * 3, p.name, sc, locked ? RGB(120, 120, 130) : RGB(235, 235, 240), 0);
      fmtR(m, sizeof m, p.price);
      if (locked) { snprintf(b, sizeof b, "УРОВЕНЬ %d", p.tier); drawText(px + pw - fs * 3, ry + rowH / 2 - fs * 3, b, fs, RGB(150, 120, 120), 2); }
      else if (button(px + pw - fs * 36, ry + fs, fs * 35, rowH - fs * 2, m, money >= p.price, RGB(60, 120, 80))) { money -= p.price; inv.push_back(list[k]); char sb[96]; snprintf(sb, sizeof sb, "КУПЛЕНО: %s (НА СКЛАДЕ)", p.name); say(sb); saveGame(); }
    }
    if (shopCat == P_COOLER) { int ry = IH - rowH - fs; if (button(px, ry, pw, rowH, "ТЕРМОПАСТА x3 - 400 Р", money >= 400, RGB(90, 90, 110))) { money -= 400; paste += 3; } }
  } else {
    // установлено в корпус - нажми, чтобы снять
    drawText(px + fs * 2, y, "В КОРПУСЕ (НАЖМИ, ЧТОБЫ СНЯТЬ):", fs, RGB(200, 200, 210), 0); y += fs * 9;
    int cw = (pw - fs) / 2, n = 0;
    for (int t = 0; t < NPT; t++) {
      bool has_ = t == P_RAM ? bd.nram > 0 : bd.part[t] >= 0; if (!has_) continue;
      if (t == P_RAM) snprintf(b, sizeof b, "ПАМЯТЬ x%d (%d ГБ)", bd.nram, ramGB()); else snprintf(b, sizeof b, "%s", PARTS[bd.part[t]].name);
      if (button(px + (n % 2) * (cw + fs), y + (n / 2) * (fs * 11), cw, fs * 10, b, true, RGB(70, 60, 50))) removePart(t);
      n++;
    }
    if (!n) { drawText(px + fs * 2, y + fs, "ПУСТО", fs, RGB(140, 140, 150), 0); n = 1; }
    y += ((n + 1) / 2) * (fs * 11) + fs * 4;
    drawText(px + fs * 2, y, "СКЛАД (НАЖМИ, ЧТОБЫ ПОСТАВИТЬ):", fs, RGB(200, 200, 210), 0); y += fs * 9;
    int perPage = imax(1, (IH - y - fs * 14) / (fs * 12)), maxPage = imax(0, ((int)inv.size() - 1) / perPage);
    page = imin(page, maxPage);
    for (int k = page * perPage; k < (int)inv.size() && k < (page + 1) * perPage; k++) {
      const Part &p = PARTS[inv[k]]; int ry = y + (k - page * perPage) * (fs * 12);
      snprintf(b, sizeof b, "%s: %s", PTN[p.type], p.name);
      if (button(px, ry, pw - fs * 26, fs * 11, b, true, RGB(50, 70, 100))) { installPart(k); break; }
      fmtR(m, sizeof m, p.price * 6 / 10); char sb[48]; snprintf(sb, sizeof sb, "ПРОДАТЬ");
      if (button(px + pw - fs * 25, ry, fs * 25, fs * 11, sb, true, RGB(110, 70, 60))) { money += p.price * 6 / 10; char mb2[96]; snprintf(mb2, sizeof mb2, "ПРОДАНО ЗА %s", m); say(mb2); inv.erase(inv.begin() + k); break; }
    }
    if (inv.empty()) drawText(px + fs * 2, y + fs, "ПУСТО: КУПИ ДЕТАЛИ В МАГАЗИНЕ", fs, RGB(140, 140, 150), 0);
    if (maxPage > 0) { if (button(px, IH - fs * 12, pw / 2 - fs, fs * 11, "< НАЗАД", page > 0, RGB(60, 66, 80))) page--; if (button(px + pw / 2, IH - fs * 12, pw / 2, fs * 11, "ДАЛЬШЕ >", page < maxPage, RGB(60, 66, 80))) page++; }
  }
}
static void drawOrderCard() {
  if (curOrder < 0) { const char *t = "ВОЗЬМИ ЗАКАЗ ВО ВКЛАДКЕ \"ЗАКАЗЫ\""; drawTextSh(fs * 4, fs * 17, t, fs, RGB(255, 230, 160), 0); return; }
  Order &o = orders[curOrder]; ReqLine r[8]; int n = orderReqs(o, r); char b[128];
  int w = IW * 40 / 100, h = fs * 12 + n * fs * 9; fillRectA(fs * 2, fs * 15, w, h, RGB(10, 14, 22), 120);   // узкая полупрозрачная карточка, чтобы не закрывать корпус
  snprintf(b, sizeof b, "%s: %s ПК", CNAMES[o.client], OTN[o.type]); drawText(fs * 4, fs * 18, b, fs, RGB(255, 230, 160), 0);
  for (int i = 0; i < n; i++) { int sc = fs; while (sc > 1 && textW(r[i].text, sc) > w - fs * 14) sc--;
    fillRect(fs * 4, fs * 27 + i * fs * 9 + fs, fs * 4, fs * 4, r[i].ok ? RGB(70, 200, 90) : RGB(220, 70, 60));
    drawText(fs * 11, fs * 27 + i * fs * 9, r[i].text, sc, r[i].ok ? RGB(200, 240, 200) : RGB(240, 210, 200), 0); }
}
static void drawActions() {
  int ax = fs * 2, aw = (IW * 62 / 100 - fs * 6) / 5, ah = imax(fs * 15, IH / 9), ay = IH - ah - fs; char b[64];
  snprintf(b, sizeof b, "ТЕРМОПАСТА\n(%d)", paste);
  if (button(ax, ay, aw - fs, ah, b, bd.part[P_CPU] >= 0 && bd.part[P_COOLER] < 0 && !bd.paste && paste > 0, RGB(110, 110, 130))) { bd.paste = true; paste--; say("ТЕРМОПАСТА НАНЕСЕНА"); }
  if (button(ax + aw, ay, aw - fs, ah, bd.cables ? "ПРОВОДА ОК" : "ПОДКЛЮЧИТЬ\nПРОВОДА", bd.part[P_PSU] >= 0 && bd.part[P_MB] >= 0 && !bd.cables, RGB(150, 120, 50))) { bd.cables = true; tested = false; say("ПРОВОДА ПОДКЛЮЧЕНЫ"); }
  if (button(ax + aw * 2, ay, aw - fs, ah, "ТЕСТ", bd.part[P_CASE] >= 0 && testT < 0, RGB(60, 110, 170))) { bd.closed = true; testT = 0; powered = true; powerT = 0; }
  if (button(ax + aw * 3, ay, aw - fs, ah, "ОТДАТЬ\nКЛИЕНТУ", tested && testBoots && curOrder >= 0, RGB(60, 150, 70))) deliver();
  if (button(ax + aw * 4, ay, aw - fs, ah, "РАЗОБРАТЬ\nВСЁ", bd.part[P_CASE] >= 0, RGB(120, 60, 50))) { removePart(P_CASE); say("ВСЁ СНЯТО НА СКЛАД"); }
}
static void drawTestResult() {
  if (testT < 0) return;
  char b[128]; int w = IW * 50 / 100, x = IW * 31 / 100 - w / 2, y = IH / 4, h;
  if (testT < 1.6f) { const char *t = testT < 0.8f ? "ВКЛЮЧАЕМ..." : "ЗАГРУЗКА СИСТЕМЫ..."; int tw2 = textW(t, fs + 1) + fs * 10; fillRectA(IW * 31 / 100 - tw2 / 2, y, tw2, fs * 14, RGB(0, 0, 0), 190); drawText(IW * 31 / 100, y + fs * 4, t, fs + 1, RGB(255, 255, 255), 1); return; }
  h = fs * 26 + (int)problems.size() * fs * 9 + (testBoots && curOrder >= 0 ? fs * 18 : 0);
  fillRectA(x, y, w, h, RGB(8, 12, 20), 225);
  drawText(x + w / 2, y + fs * 4, testBoots ? "КОМПЬЮТЕР РАБОТАЕТ!" : "НЕ ЗАПУСКАЕТСЯ", fs + 1, testBoots ? RGB(120, 240, 140) : RGB(255, 100, 90), 1);
  int yy = y + fs * 15;
  if (testBoots && curOrder >= 0) { Order &o = orders[curOrder];
    snprintf(b, sizeof b, "ТЕСТ: %d FPS В \"%s\", ПРОЦЕССОР %d", estFps(o.game), GAMES[o.game], (int)cpuScore()); drawText(x + w / 2, yy, b, fs, RGB(220, 230, 255), 1); yy += fs * 9;
    snprintf(b, sizeof b, "ПАМЯТЬ %d ГБ, ДИСК %d ГБ%s", ramGB(), PT(P_SSD) ? PT(P_SSD)->a : 0, hasRgb() ? ", RGB" : ""); drawText(x + w / 2, yy, b, fs, RGB(220, 230, 255), 1); yy += fs * 9; }
  for (size_t i = 0; i < problems.size(); i++) { int sc = fs; while (sc > 1 && textW(problems[i].c_str(), sc) > w - fs * 6) sc--; drawText(x + w / 2, yy, problems[i].c_str(), sc, RGB(255, 190, 150), 1); yy += fs * 9; }
  if (button(x + w / 2 - fs * 20, y + h - fs * 2, fs * 40, fs * 11, "ЗАКРЫТЬ", true, RGB(70, 80, 100))) testT = -1;
}
static void frameGame(float dt) {
  if (msgT > 0) msgT -= dt;
  for (size_t i = 0; i < anims.size();) { anims[i].t += dt; if (anims[i].t >= 0.6f) anims.erase(anims.begin() + i); else i++; }
  if (testT >= 0) { testT += dt; powerT += dt;
    if (testT >= 1.6f && !tested) { checkBuild(problems, testBoots); tested = true; if (!testBoots) powered = false; } }
  else if (powered) { powerT += dt; if (powerT > 6) powered = false; }
  // вращение камеры пальцем по левой части экрана
  if (P.down && P.sx < IW * 62 / 100 && P.sy > fs * 14 && P.sy < IH - IH / 8) { camYaw += P.dx * 0.008f; camPitch = clampf(camPitch + P.dy * 0.006f, 0.05f, 1.2f); }
  int vw = IW * 62 / 100;
  setClipRect(0, 0, IW, IH);
  V3 tgt = v3(0.0f, 0.38f, 0.0f), eye = tgt + v3(sinf(camYaw) * cosf(camPitch), sinf(camPitch), cosf(camYaw) * cosf(camPitch)) * camDist;
  setCamera(eye, tgt); focal = IW * 0.55f; vcx = vw * 0.58f; vcy = IH * 0.56f;
  fogCol = RGB(70, 76, 90); fogA = 1e9f; fogB = 2e9f; gLight = 1; gNight = 0; LDIR = vnorm(v3(-0.4f, 0.85f, -0.3f));
  fillSky(); clearDepth(); drawScene();
  // верхняя строка
  char b[96], m[32];
  fillRectA(0, 0, IW, fs * 13, RGB(14, 16, 22), 220);
  fmtR(m, sizeof m, money); snprintf(b, sizeof b, "ЗАКАЗ %d   %s", day, m); drawTextSh(fs * 3, fs * 3, b, fs + 1, RGB(150, 240, 150), 0);
  snprintf(b, sizeof b, "УРОВЕНЬ %d   РЕПУТАЦИЯ %d", level, rep); drawTextSh(fs * 3 + textW("ЗАКАЗ №000   0 000 000 Р", fs + 1) + fs * 10, fs * 3, b, fs + 1, RGB(255, 220, 90), 0);
  drawOrderCard(); drawActions(); drawPanel(); drawTestResult();
  if (msgT > 0) { int sc = fs + 1; while (sc > 1 && textW(msg, sc) > vw - fs * 8) sc--; int w = textW(msg, sc) + fs * 8; fillRectA(vw / 2 - w / 2, IH - IH / 8 - fs * 16, w, fs * 12, RGB(0, 0, 0), 180); drawText(vw / 2, IH - IH / 8 - fs * 13, msg, sc, RGB(255, 255, 255), 1); }
}
enum { GS_TITLE, GS_GAME };
static int gs = GS_TITLE; static bool hasSave = false;
static void frameTitle() {
  int vw = IW; V3 tgt = v3(0, 0.4f, 0), eye = tgt + v3(sinf(gTime * 0.3f) * 1.9f, 0.7f, cosf(gTime * 0.3f) * 1.9f);
  setClipRect(0, 0, IW, IH); setCamera(eye, tgt); focal = IW * 0.6f; vcx = vw * 0.5f; vcy = IH * 0.55f; fogCol = RGB(40, 44, 56); fogA = 1e9f; fogB = 2e9f; gLight = 1; LDIR = vnorm(v3(-0.4f, 0.85f, -0.3f));
  fillSky(); clearDepth(); drawScene();
  fillRectA(0, IH / 12, IW, IH / 4, RGB(0, 0, 0), 150);
  drawTextSh(IW / 2, IH / 10, "СБОРКА ПК", fs * 3, RGB(120, 200, 255), 1);
  drawText(IW / 2, IH / 10 + fs * 26, "СОБИРАЙ КОМПЬЮТЕРЫ НА ЗАКАЗ И РАСТИ ОТ ОФИСНЫХ ДО ТОПОВЫХ", fs, RGB(235, 235, 240), 1);
  int bw = IW / 3, bh = IH / 7, y = IH / 2;
  if (hasSave) { if (button(IW / 2 - bw / 2, y, bw, bh, "ПРОДОЛЖИТЬ", true, RGB(60, 150, 70))) gs = GS_GAME; y += bh + fs * 3; }
  if (button(IW / 2 - bw / 2, y, bw, bh, hasSave ? "НОВАЯ ИГРА" : "НАЧАТЬ", true, hasSave ? RGB(120, 80, 60) : RGB(60, 150, 70))) { newGame(); saveGame(); hasSave = true; gs = GS_GAME; say("ВОЗЬМИ ПЕРВЫЙ ЗАКАЗ: ОФИСНЫЙ ПК - САМЫЙ ПРОСТОЙ"); }
}

int main(int argc, char **argv) {
  (void)argc; (void)argv;
  if (!gfxInit()) return 1;
#if defined(__ANDROID__) && defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION >= 2
  { const char *ip = SDL_AndroidGetInternalStoragePath(); if (ip && *ip) snprintf(gBase, sizeof gBase, "%s/", ip); }
#endif
  rotMode = SH > SW; LW = rotMode ? SH : SW; LH = rotMode ? SW : SH;
  computeSize(); if (!allocBuffers()) return 1;
  srand((unsigned)time(NULL));
  newGame(); hasSave = loadGame(); if (!hasSave) newGame();
  memset(&P, 0, sizeof P);
  bool running = true; Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    float dt = clampf((now - last) / 1000.0f, 0.001f, 0.05f); last = now; gTime += dt;
    if (gfxCheckResize()) { rotMode = SH > SW; LW = rotMode ? SH : SW; LH = rotMode ? SW : SH; computeSize(); if (!allocBuffers()) break; }
    P.pressed = false; P.clicked = false; P.dx = P.dy = 0;
    Ev e;
    while (pollEv(e)) {
      if (e.type == 0) running = false;
      else if (e.type == 5) { if (gs == GS_GAME) saveGame(); }
      else if (e.type == 4) { if (gs == GS_TITLE) running = false; else if (testT >= 0) testT = -1; else { saveGame(); gs = GS_TITLE; } }
      else {
        int mx, my; mapPt(e.x, e.y, mx, my);
        if (e.type == 1) { P.down = true; P.pressed = true; P.x = P.sx = mx; P.y = P.sy = my; }
        else if (e.type == 3) { if (P.down) { P.dx += mx - P.x; P.dy += my - P.y; } P.x = mx; P.y = my; }
        else if (e.type == 2) { P.x = mx; P.y = my; P.cx = mx; P.cy = my; P.down = false; if (abs(mx - P.sx) + abs(my - P.sy) < fs * 8) P.clicked = true; }
      }
    }
    if (gs == GS_TITLE) frameTitle(); else frameGame(dt);
    gfxPresent();
    Uint32 spent = SDL_GetTicks() - now; if (spent < 16) SDL_Delay(16 - spent);
  }
  if (gs == GS_GAME) saveGame();
  gfxQuit();
  return 0;
}
