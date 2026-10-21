// =====================================================================
//  СБОРКА ПК — мастерская компьютеров: бери заказы, покупай детали, собирай ПК в 3D, тестируй и сдавай
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
#include <cstddef>
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
// ---- видеокарта (OpenGL ES 2): 3D рисует GPU, а 2D-интерфейс кладётся поверх прозрачной картинкой ----
static bool gGL = false;          // рисуем ли 3D видеокартой
static bool gWantGL = false;      // пробовать ли видеокарту при запуске (можно выключить в меню)
static bool gOverlay = false;     // fb - прозрачный слой интерфейса поверх 3D (альфа в старшем байте)
struct GV { float x, y, z; unsigned char r, g, b, a; };
static std::vector<GV> gVerts;    // треугольники 3D за кадр (в координатах камеры)
static unsigned gSkyCol = 0;
static float gNearZ = 0.3f;   // ближе этой дистанции полигоны обрезаются (в квартире - 0.06, чтобы стены не просвечивали)
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
static int LW = 480, LH = 800;      // логический размер экрана (игра вертикальная)
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
static SDL_GLContext gCtx = NULL;

// ======================= OpenGL ES 2 =======================
typedef unsigned int GLenum; typedef unsigned int GLuint; typedef int GLint; typedef int GLsizei; typedef float GLfloat; typedef unsigned char GLboolean; typedef char GLchar; typedef unsigned int GLbitfield; typedef ptrdiff_t GLsizeiptr;
#define GLF(ret, name, args) typedef ret (*PFN_##name) args; static PFN_##name p_##name = NULL;
GLF(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) GLF(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat)) GLF(void, glClear, (GLbitfield))
GLF(void, glEnable, (GLenum)) GLF(void, glDisable, (GLenum)) GLF(void, glDepthFunc, (GLenum)) GLF(void, glDepthMask, (GLboolean)) GLF(void, glBlendFunc, (GLenum, GLenum))
GLF(GLuint, glCreateShader, (GLenum)) GLF(void, glShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *)) GLF(void, glCompileShader, (GLuint))
GLF(void, glGetShaderiv, (GLuint, GLenum, GLint *)) GLF(GLuint, glCreateProgram, (void)) GLF(void, glAttachShader, (GLuint, GLuint))
GLF(void, glBindAttribLocation, (GLuint, GLuint, const GLchar *)) GLF(void, glLinkProgram, (GLuint)) GLF(void, glGetProgramiv, (GLuint, GLenum, GLint *))
GLF(void, glUseProgram, (GLuint)) GLF(GLint, glGetUniformLocation, (GLuint, const GLchar *)) GLF(void, glUniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))
GLF(void, glUniform2f, (GLint, GLfloat, GLfloat)) GLF(void, glUniform1i, (GLint, GLint)) GLF(void, glBindBuffer, (GLenum, GLuint))
GLF(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void *)) GLF(void, glEnableVertexAttribArray, (GLuint)) GLF(void, glDisableVertexAttribArray, (GLuint))
GLF(void, glDrawArrays, (GLenum, GLint, GLsizei)) GLF(void, glGenTextures, (GLsizei, GLuint *)) GLF(void, glBindTexture, (GLenum, GLuint)) GLF(void, glDeleteTextures, (GLsizei, const GLuint *))
GLF(void, glTexParameteri, (GLenum, GLenum, GLint)) GLF(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))
GLF(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *)) GLF(void, glActiveTexture, (GLenum)) GLF(void, glPixelStorei, (GLenum, GLint))
GLF(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *))
#undef GLF
static GLuint glProg3D = 0, glProg2D = 0, glTexUI = 0; static GLint uP = -1, uNF = -1, uTex = -1; static int glTexW = 0, glTexH = 0, glVW = 0, glVH = 0;
static GLuint glMakeProg(const char *vs, const char *fs2) {
  GLuint v = p_glCreateShader(0x8B31), f = p_glCreateShader(0x8B30); GLint ok = 0;
  p_glShaderSource(v, 1, &vs, NULL); p_glCompileShader(v); p_glGetShaderiv(v, 0x8B81, &ok); if (!ok) return 0;
  p_glShaderSource(f, 1, &fs2, NULL); p_glCompileShader(f); p_glGetShaderiv(f, 0x8B81, &ok); if (!ok) return 0;
  GLuint p = p_glCreateProgram(); p_glAttachShader(p, v); p_glAttachShader(p, f);
  p_glBindAttribLocation(p, 0, "aP"); p_glBindAttribLocation(p, 1, "aC"); p_glLinkProgram(p); p_glGetProgramiv(p, 0x8B82, &ok);
  return ok ? p : 0;
}
// loader - функция, которая по имени даёт адрес функции GL (SDL_GL_GetProcAddress или eglGetProcAddress)
static bool glSetup(void *(*loader)(const char *)) {
  #define LD(n) p_##n = (PFN_##n)loader(#n); if (!p_##n) return false;
  LD(glViewport) LD(glClearColor) LD(glClear) LD(glEnable) LD(glDisable) LD(glDepthFunc) LD(glDepthMask) LD(glBlendFunc) LD(glCreateShader) LD(glShaderSource)
  LD(glCompileShader) LD(glGetShaderiv) LD(glCreateProgram) LD(glAttachShader) LD(glBindAttribLocation) LD(glLinkProgram) LD(glGetProgramiv) LD(glUseProgram)
  LD(glGetUniformLocation) LD(glUniform4f) LD(glUniform2f) LD(glUniform1i) LD(glBindBuffer) LD(glVertexAttribPointer) LD(glEnableVertexAttribArray)
  LD(glDisableVertexAttribArray) LD(glDrawArrays) LD(glGenTextures) LD(glBindTexture) LD(glDeleteTextures) LD(glTexParameteri) LD(glTexImage2D) LD(glTexSubImage2D)
  LD(glActiveTexture) LD(glPixelStorei)
  #undef LD
  p_glReadPixels = (PFN_glReadPixels)loader("glReadPixels");
  // 3D: точки уже в координатах камеры; проекция как у программного движка (focal, vcx, vcy), глубина - видеокартой
  glProg3D = glMakeProg(
    "attribute vec3 aP; attribute vec4 aC; uniform vec4 uP; uniform vec2 uNF; varying vec4 vC;\n"
    "void main(){ vC = aC; gl_Position = vec4(aP.x * uP.x + uP.y * aP.z, aP.y * uP.z + uP.w * aP.z, aP.z * uNF.x + uNF.y, aP.z); }\n",
    "precision mediump float; varying vec4 vC; void main(){ gl_FragColor = vC; }\n");
  // 2D: картинка интерфейса поверх (в памяти байты B,G,R,A - переставляем)
  glProg2D = glMakeProg(
    "attribute vec2 aP; varying vec2 vT; void main(){ vT = vec2(aP.x * 0.5 + 0.5, 0.5 - aP.y * 0.5); gl_Position = vec4(aP, 0.0, 1.0); }\n",
    "precision mediump float; varying vec2 vT; uniform sampler2D uTex; void main(){ vec4 t = texture2D(uTex, vT); gl_FragColor = vec4(t.b, t.g, t.r, t.a); }\n");
  if (!glProg3D || !glProg2D) return false;
  uP = p_glGetUniformLocation(glProg3D, "uP"); uNF = p_glGetUniformLocation(glProg3D, "uNF"); uTex = p_glGetUniformLocation(glProg2D, "uTex");
  p_glGenTextures(1, &glTexUI);
  return true;
}
static void glUITex(int w, int h) {
  if (glTexW == w && glTexH == h) return;
  p_glBindTexture(0x0DE1, glTexUI);
  p_glTexParameteri(0x0DE1, 0x2801, 0x2600); p_glTexParameteri(0x0DE1, 0x2800, 0x2600);      // NEAREST: пиксельный шрифт чёткий
  p_glTexParameteri(0x0DE1, 0x2802, 0x812F); p_glTexParameteri(0x0DE1, 0x2803, 0x812F);
  p_glTexImage2D(0x0DE1, 0, 0x1908, w, h, 0, 0x1908, 0x1401, NULL); glTexW = w; glTexH = h;
}
// кадр: очистка, все 3D-треугольники одним вызовом, потом интерфейс поверх
static void glFrame(int vw, int vh) {
  p_glViewport(0, 0, vw, vh);
  p_glClearColor(((gSkyCol >> 16) & 255) / 255.0f, ((gSkyCol >> 8) & 255) / 255.0f, (gSkyCol & 255) / 255.0f, 1.0f);
  p_glClear(0x4000 | 0x100);
  if (!gVerts.empty()) {
    p_glEnable(0x0B71); p_glDepthFunc(0x0203); p_glDepthMask(1); p_glDisable(0x0BE2); p_glDisable(0x0B44);
    p_glUseProgram(glProg3D); p_glBindBuffer(0x8892, 0);
    float n = fmaxv(0.02f, gNearZ), f = 120.0f;
    p_glUniform4f(uP, 2.0f * focal / IW, 2.0f * vcx / IW - 1.0f, 2.0f * focal / IH, 1.0f - 2.0f * vcy / IH);
    p_glUniform2f(uNF, (f + n) / (f - n), -2.0f * f * n / (f - n));
    p_glEnableVertexAttribArray(0); p_glEnableVertexAttribArray(1);
    p_glVertexAttribPointer(0, 3, 0x1406, 0, sizeof(GV), &gVerts[0].x);
    p_glVertexAttribPointer(1, 4, 0x1401, 1, sizeof(GV), &gVerts[0].r);
    p_glDrawArrays(4, 0, (GLsizei)gVerts.size());
    p_glDisableVertexAttribArray(1);
  }
  p_glDisable(0x0B71); p_glEnable(0x0BE2); p_glBlendFunc(0x0302, 0x0303);
  p_glUseProgram(glProg2D); p_glActiveTexture(0x84C0); glUITex(IW, IH); p_glBindTexture(0x0DE1, glTexUI);
  p_glPixelStorei(0x0CF5, 4); p_glTexSubImage2D(0x0DE1, 0, 0, 0, IW, IH, 0x1908, 0x1401, fb); p_glUniform1i(uTex, 0);
  static const float quad[8] = {-1, -1, 1, -1, -1, 1, 1, 1};
  p_glEnableVertexAttribArray(0); p_glVertexAttribPointer(0, 2, 0x1406, 0, 0, quad); p_glDrawArrays(5, 0, 4);
  gVerts.clear();
  memset(fb, 0, sizeof(u32) * IW * IH);          // интерфейс следующего кадра - на прозрачном фоне
}

static void *sdlGLProc(const char *n) { return SDL_GL_GetProcAddress(n); }
static bool gfxInit() {
  if (SDL_Init(SDL_INIT_VIDEO) < 0) return false;
  SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  if (gWantGL) {                                   // пробуем видеокарту; не вышло - обычный режим
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    gWin = SDL_CreateWindow("Bitva bashen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (gWin) { gCtx = SDL_GL_CreateContext(gWin);
      if (!gCtx) { SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16); gCtx = SDL_GL_CreateContext(gWin); }
      if (gCtx && glSetup(sdlGLProc)) { SDL_GL_SetSwapInterval(1); SDL_GetWindowSize(gWin, &SW, &SH); gGL = true; gOverlay = true; return true; }
      if (gCtx) SDL_GL_DeleteContext(gCtx); gCtx = NULL; SDL_DestroyWindow(gWin); gWin = NULL; }
  }
  gWin = SDL_CreateWindow("Bitva bashen", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 0, 0,
                          SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
  if (!gWin) return false;
  SDL_GetWindowSize(gWin, &SW, &SH);
  gRen = SDL_CreateRenderer(gWin, -1, 0);
  if (!gRen) return false;
  return true;
}
static void gfxTex() {
  if (gGL) return;
  if (gTex) SDL_DestroyTexture(gTex);
  gTex = SDL_CreateTexture(gRen, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, IW, IH);
}
static void gfxResize() {}
static void gfxPresent() {
  if (gGL) { int dw = SW, dh = SH; SDL_GL_GetDrawableSize(gWin, &dw, &dh); glFrame(dw, dh); SDL_GL_SwapWindow(gWin); return; }
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
static const char *CFGF = "sborka.cfg";
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
    if (gOverlay) { u32 cc = c | 0xFF000000u; for (int i = x0; i < x1; i++) r[i] = cc; }
    else for (int i = x0; i < x1; i++) r[i] = c;
  }
}
static void fillRectA(int x, int y, int w, int h, u32 c, int a) {   // a: 0..255
  int x0 = imax(x, 0), y0 = imax(y, 0), x1 = imin(x + w, IW), y1 = imin(y + h, IH);
  int ia = 255 - a;
  for (int j = y0; j < y1; j++) {
    u32 *r = fb + j * IW;
    for (int i = x0; i < x1; i++) {
      u32 d = r[i];
      if (gOverlay) { u32 da = d >> 24; if (!da) { r[i] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        r[i] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
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
      if (gOverlay) { u32 da = d >> 24; if (!da) { fb[y * IW + x] = (c & 0xFFFFFFu) | ((u32)a << 24); continue; }
        fb[y * IW + x] = RGB((CR(d) * ia + CR(c) * a) / 255, (CG(d) * ia + CG(c) * a) / 255, (CB(d) * ia + CB(c) * a) / 255) | ((da + (255 - da) * a / 255) << 24); continue; }
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
  if (gGL) {
    if (shadowK || n < 3) return;
    bool any = false; for (int i = 0; i < n; i++) if (in[i].z > gNearZ) { any = true; break; } if (!any) return;   // целиком за камерой
    unsigned char cr = (col >> 16) & 255, cg = (col >> 8) & 255, cb = col & 255;
    for (int i = 1; i + 1 < n; i++) { const V3 *t[3] = {&in[0], &in[i], &in[i + 1]};
      for (int k = 0; k < 3; k++) { GV g; g.x = t[k]->x; g.y = t[k]->y; g.z = t[k]->z; g.r = cr; g.g = cg; g.b = cb; g.a = 255; gVerts.push_back(g); } }
    return;
  }
  V3 v[32];
  int m = 0;
  const float NZ = gNearZ;
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
  const float NZ = gNearZ;
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
  if (gGL) return;
  if (clipX0 == 0 && clipX1 == IW) { memset(zb + clipY0 * IW, 0, sizeof(float) * (size_t)(clipY1 - clipY0) * IW); return; }
  for (int y = clipY0; y < clipY1; y++) { float *r = zb + y * IW; for (int x = clipX0; x < clipX1; x++) r[x] = 0; }
}
static void fillSky() {
  if (gGL) { gSkyCol = fogCol; return; }
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
//  СБОРКА ПК: детали, заказы, сохранение
// =====================================================================
enum { PT_CASE, PT_MB, PT_CPU, PT_COOL, PT_RAM, PT_SSD, PT_GPU, PT_PSU, NPT };
static const char *PTN[NPT] = {"КОРПУС", "МАТ. ПЛАТА", "ПРОЦЕССОР", "КУЛЕР", "ПАМЯТЬ", "SSD", "ВИДЕОКАРТА", "БЛОК ПИТАНИЯ"};
// a, b, c, d - смысл зависит от типа:
//  КОРПУС: a - обдув (100 = обычный), b - подсветка, c - 1 только mATX, d - длина видеокарты, мм
//  ПЛАТА:  a - сокет (0 А, 1 Б), b - память (4/5), c - 1 mATX
//  ПРОЦ:   a - сокет, b - мощность, c - тепло, Вт
//  КУЛЕР:  a - вид (0 боксовый, 1 башня, 2 двойная башня, 3 водянка 240, 4 водянка 360), b - подсветка, c - отводит Вт
//  ПАМЯТЬ: a - частота, b - тип, c - ГБ
//  SSD:    c - ГБ
//  ВИДЕО:  b - мощность, c - потребление, Вт, d - длина, мм
//  БП:     c - Вт
struct Part { const char *name; int type, price, tier, a, b, c, d; u32 col; };
static const Part PARTS[] = {
  {"КУБИК МИНИ",        PT_CASE, 3000,  0, 85,  0, 1, 250, RGB(60, 62, 70)},
  {"СТАНДАРТ",          PT_CASE, 4500,  0, 100, 0, 0, 300, RGB(40, 42, 48)},
  {"АЭРО СЕТКА",        PT_CASE, 7500,  1, 120, 0, 0, 340, RGB(225, 228, 232)},
  {"БАШНЯ RGB",         PT_CASE, 12000, 2, 115, 1, 0, 400, RGB(28, 28, 34)},
  {"ПЛАТА А-310 МИНИ",  PT_MB,   5000,  0, 0, 4, 1, 0, RGB(30, 90, 60)},
  {"ПЛАТА А-550",       PT_MB,   8000,  0, 0, 4, 0, 0, RGB(35, 40, 45)},
  {"ПЛАТА А-770 ПРО",   PT_MB,   15000, 2, 0, 5, 0, 0, RGB(25, 25, 30)},
  {"ПЛАТА Б-410 МИНИ",  PT_MB,   5500,  0, 1, 4, 1, 0, RGB(30, 60, 100)},
  {"ПЛАТА Б-660",       PT_MB,   11000, 1, 1, 5, 0, 0, RGB(40, 40, 50)},
  {"ПЛАТА Б-790 ЭКСТРИМ",PT_MB,  19000, 3, 1, 5, 0, 0, RGB(20, 20, 24)},
  {"КВАНТ К3",          PT_CPU,  6500,  0, 0, 30, 65, 0, RGB(200, 90, 40)},
  {"КВАНТ К5",          PT_CPU,  13000, 0, 0, 55, 95, 0, RGB(200, 90, 40)},
  {"КВАНТ К7",          PT_CPU,  24000, 2, 0, 80, 125, 0, RGB(200, 90, 40)},
  {"КВАНТ К9",          PT_CPU,  42000, 4, 0, 100, 170, 0, RGB(200, 90, 40)},
  {"ВЕКТОР В3",         PT_CPU,  6900,  0, 1, 32, 65, 0, RGB(50, 110, 210)},
  {"ВЕКТОР В5",         PT_CPU,  14500, 1, 1, 58, 105, 0, RGB(50, 110, 210)},
  {"ВЕКТОР В7",         PT_CPU,  27000, 3, 1, 85, 140, 0, RGB(50, 110, 210)},
  {"ВЕКТОР В9",         PT_CPU,  48000, 5, 1, 105, 200, 0, RGB(50, 110, 210)},
  {"КУЛЕР ПРОСТОЙ",     PT_COOL, 800,   0, 0, 0, 90, 0, RGB(60, 60, 65)},
  {"БАШНЯ 120",         PT_COOL, 2500,  0, 1, 0, 150, 0, RGB(180, 185, 195)},
  {"ДВОЙНАЯ БАШНЯ",     PT_COOL, 5000,  1, 2, 0, 200, 0, RGB(40, 40, 46)},
  {"ВОДЯНКА 240",       PT_COOL, 9000,  2, 3, 0, 250, 0, RGB(30, 30, 36)},
  {"ВОДЯНКА 360 RGB",   PT_COOL, 14000, 3, 4, 1, 320, 0, RGB(235, 235, 240)},
  {"DDR4 8 ГБ",         PT_RAM,  1800,  0, 2666, 4, 8, 0, RGB(40, 120, 60)},
  {"DDR4 16 ГБ",        PT_RAM,  3500,  0, 3200, 4, 16, 0, RGB(40, 40, 48)},
  {"DDR4 32 ГБ",        PT_RAM,  7000,  1, 3600, 4, 32, 0, RGB(180, 40, 40)},
  {"DDR5 16 ГБ",        PT_RAM,  5000,  1, 5200, 5, 16, 0, RGB(40, 40, 48)},
  {"DDR5 32 ГБ RGB",    PT_RAM,  9500,  2, 6000, 5, 32, 0, RGB(235, 235, 240)},
  {"DDR5 64 ГБ RGB",    PT_RAM,  19000, 4, 6400, 5, 64, 0, RGB(25, 25, 30)},
  {"SSD 256 ГБ",        PT_SSD,  2000,  0, 0, 0, 256, 0, RGB(30, 30, 34)},
  {"SSD 512 ГБ",        PT_SSD,  3500,  0, 0, 0, 512, 0, RGB(30, 30, 34)},
  {"SSD 1 ТБ",          PT_SSD,  6000,  1, 0, 0, 1024, 0, RGB(40, 40, 120)},
  {"SSD 2 ТБ",          PT_SSD,  11000, 3, 0, 0, 2048, 0, RGB(120, 30, 30)},
  {"ГРОМ 1630",         PT_GPU,  9000,  0, 0, 20, 75, 180, RGB(60, 60, 66)},
  {"ГРОМ 3050",         PT_GPU,  18000, 0, 0, 45, 130, 230, RGB(40, 40, 46)},
  {"ПИКСЕЛЬ 6600",      PT_GPU,  22000, 1, 0, 55, 160, 240, RGB(150, 30, 30)},
  {"ГРОМ 4060",         PT_GPU,  30000, 2, 0, 70, 170, 250, RGB(30, 30, 34)},
  {"ПИКСЕЛЬ 7800",      PT_GPU,  45000, 3, 0, 85, 260, 300, RGB(170, 30, 30)},
  {"ГРОМ 4080",         PT_GPU,  85000, 4, 0, 100, 320, 330, RGB(30, 30, 34)},
  {"ГРОМ 4090 ТИТАН",   PT_GPU,  150000,5, 0, 130, 450, 350, RGB(200, 160, 40)},
  {"БП 400 ВТ",         PT_PSU,  3500,  0, 0, 0, 400, 0, RGB(30, 30, 34)},
  {"БП 550 ВТ",         PT_PSU,  5000,  0, 0, 0, 550, 0, RGB(30, 30, 34)},
  {"БП 750 ВТ ЗОЛОТО",  PT_PSU,  8000,  1, 0, 0, 750, 0, RGB(30, 30, 34)},
  {"БП 1000 ВТ",        PT_PSU,  13000, 3, 0, 0, 1000, 0, RGB(30, 30, 34)},
  {"БП 1300 ВТ",        PT_PSU,  20000, 4, 0, 0, 1300, 0, RGB(30, 30, 34)},
};
static const int NPART = (int)(sizeof PARTS / sizeof PARTS[0]);
static const int MAXPART = 64;
static void partInfo(int i, char *b, int n) {                    // короткие характеристики для списка
  const Part &p = PARTS[i];
  switch (p.type) {
    case PT_CASE: snprintf(b, n, "%s, ВИДЕО ДО %d ММ, ОБДУВ %d%%%s", p.c ? "ТОЛЬКО mATX" : "ATX И mATX", p.d, p.a, p.b ? ", RGB" : ""); break;
    case PT_MB:   snprintf(b, n, "СОКЕТ %s, DDR%d, %s", p.a ? "Б" : "А", p.b, p.c ? "mATX" : "ATX"); break;
    case PT_CPU:  snprintf(b, n, "СОКЕТ %s, МОЩНОСТЬ %d, ТЕПЛО %d ВТ", p.a ? "Б" : "А", p.b, p.c); break;
    case PT_COOL: snprintf(b, n, "ОТВОДИТ %d ВТ%s", p.c, p.b ? ", RGB" : ""); break;
    case PT_RAM:  snprintf(b, n, "DDR%d, %d ГБ, %d МГЦ%s", p.b, p.c, p.a, strstr(p.name, "RGB") ? ", RGB" : ""); break;
    case PT_SSD:  snprintf(b, n, "%d ГБ", p.c); break;
    case PT_GPU:  snprintf(b, n, "МОЩНОСТЬ %d, %d ВТ, ДЛИНА %d ММ", p.b, p.c, p.d); break;
    case PT_PSU:  snprintf(b, n, "%d ВТ", p.c); break;
  }
}
static void fmtMoney(char *b, int n, long long v) {             // 12 500 Р
  char t[32]; snprintf(t, sizeof t, "%lld", v < 0 ? -v : v); int L = (int)strlen(t), o = 0; char r[48];
  if (v < 0) r[o++] = '-';
  for (int i = 0; i < L; i++) { r[o++] = t[i]; if ((L - i - 1) % 3 == 0 && i < L - 1) r[o++] = ' '; } r[o] = 0; snprintf(b, n, "%s Р", r);
}

// ---- заказы ----
enum { OT_OFFICE, OT_STUDY, OT_GAME1, OT_MINI, OT_GAME2, OT_EDIT, OT_STREAM, OT_TOP, NOT };
struct OrderType { const char *name; int lvl, minScore, pay, needGpu, minRam, mini, rgb, minSsd, minCpu; };
static const OrderType OTYPES[NOT] = {
  {"ОФИСНЫЙ ПК",          0, 1000,  28000,  0, 8,  0, 0, 256,  0},
  {"ПК ДЛЯ УЧЁБЫ",        0, 1500,  34000,  0, 16, 0, 0, 512,  0},
  {"ИГРОВОЙ НАЧАЛЬНЫЙ",   0, 4500,  68000,  1, 16, 0, 0, 512,  0},
  {"МАЛЕНЬКИЙ ИГРОВОЙ",   1, 4500,  75000,  1, 16, 1, 0, 512,  0},
  {"ИГРОВОЙ ПК",          2, 6800,  110000, 1, 16, 0, 0, 1024, 0},
  {"ДЛЯ МОНТАЖА ВИДЕО",   2, 6000,  120000, 1, 32, 0, 0, 1024, 80},
  {"ПК СТРИМЕРА",         3, 8500,  165000, 1, 32, 0, 1, 1024, 0},
  {"ТОПОВЫЙ МОНСТР",      4, 12000, 330000, 1, 64, 0, 1, 2048, 100},
};
static const char *CLIENTS[16] = {"ИВАН", "МАРИНА", "ДЕД ПЕТРОВИЧ", "СТУДЕНТ ОЛЕГ", "АННА", "КИБЕРКЛУБ", "БУХГАЛТЕРИЯ", "БЛОГЕР ДИМА", "ШКОЛЬНИК ВАНЯ", "СЕРГЕЙ", "ДИЗАЙНЕР ЛЕНА", "СТРИМЕР КОТ", "ВЕБ-СТУДИЯ", "ПАПА АРТЁМА", "ГЕЙМЕР МАКС", "КАТЯ"};
struct Order { int type, client, pay; bool live; };

// ---- профиль и сохранение ----
static const int NOFFER = 3;
struct Save {
  long long money; int rep, day, delivered, best; int inv[MAXPART]; int slot[NPT]; int cab[3];
  Order offer[NOFFER]; int active; bool soundOn; int tutorial;
};
static Save S;
static int repLevel() { return clampi(S.rep / 20, 0, 5); }
static void newOrder(Order &o) {
  int lv = repLevel(), cand[NOT], n = 0; for (int t = 0; t < NOT; t++) if (OTYPES[t].lvl <= lv) cand[n++] = t;
  int t = cand[rand() % n]; if (n > 2 && rand() % 3 == 0) t = cand[n - 1 - rand() % 2];      // чаще - заказы своего уровня
  o.type = t; o.client = rand() % 16; o.pay = OTYPES[t].pay * (90 + rand() % 25) / 100 / 100 * 100; o.live = true;
}
static void newGame() {
  memset(&S, 0, sizeof S); S.money = 40000; S.day = 1; S.soundOn = true; S.active = -1; S.tutorial = 1;
  for (int i = 0; i < NPT; i++) S.slot[i] = -1;
  for (int k = 0; k < NOFFER; k++) newOrder(S.offer[k]);
  S.offer[0].type = OT_OFFICE; S.offer[0].pay = 28000;                                       // первый заказ - простой
}
static const unsigned SAVE_MAGIC = 0x5B0B0001u;
static unsigned sum32(const void *p, size_t n) { const unsigned char *b = (const unsigned char *)p; unsigned h = 2166136261u; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 16777619u; } return h; }
static bool writeTo(const char *fn) {
  FILE *f = fopen(fn, "wb"); if (!f) return false; unsigned m = SAVE_MAGIC, cs = sum32(&S, sizeof S); int sz = (int)sizeof S;
  bool ok = fwrite(&m, 4, 1, f) == 1 && fwrite(&sz, 4, 1, f) == 1 && fwrite(&S, sizeof S, 1, f) == 1 && fwrite(&cs, 4, 1, f) == 1;
  ok = fflush(f) == 0 && ok; ok = fclose(f) == 0 && ok; return ok;
}
static void saveGame() {                                         // надёжно: временный файл, потом подмена; старое - в запас
  char tmp[600], sav[600], bak[600]; snprintf(tmp, sizeof tmp, "%s", pth("sborka.tmp")); snprintf(sav, sizeof sav, "%s", pth("sborka.sav")); snprintf(bak, sizeof bak, "%s", pth("sborka.bak"));
  if (!writeTo(tmp)) { writeTo(sav); return; } remove(bak); rename(sav, bak); if (rename(tmp, sav) != 0) writeTo(sav);
}
static bool loadFrom(const char *fn) {
  FILE *f = fopen(fn, "rb"); if (!f) return false; unsigned m = 0, cs = 0; int sz = 0; static Save t;
  bool ok = fread(&m, 4, 1, f) == 1 && fread(&sz, 4, 1, f) == 1 && m == SAVE_MAGIC && sz == (int)sizeof t && fread(&t, sizeof t, 1, f) == 1 && fread(&cs, 4, 1, f) == 1 && cs == sum32(&t, sizeof t);
  fclose(f); if (!ok) return false; S = t;
  for (int i = 0; i < NPT; i++) if (S.slot[i] < -1 || S.slot[i] >= NPART) S.slot[i] = -1;
  for (int i = 0; i < MAXPART; i++) S.inv[i] = clampi(S.inv[i], 0, 99);
  if (S.active < -1 || S.active >= NOFFER) S.active = -1;
  for (int k = 0; k < NOFFER; k++) if (S.offer[k].type < 0 || S.offer[k].type >= NOT) newOrder(S.offer[k]);
  return true;
}
static bool loadGame() { if (loadFrom(pth("sborka.sav"))) return true; if (loadFrom(pth("sborka.bak"))) return true; return loadFrom(pth("sborka.tmp")); }

// ---- проверка сборки ----
static int SP(int t) { return S.slot[t]; }
static const Part *PP(int t) { return S.slot[t] >= 0 ? &PARTS[S.slot[t]] : NULL; }
static bool hasRgb() { return (PP(PT_CASE) && PP(PT_CASE)->b) || (PP(PT_COOL) && PP(PT_COOL)->b) || (PP(PT_RAM) && strstr(PP(PT_RAM)->name, "RGB")); }
static int powerNeed() { int w = 60; if (PP(PT_CPU)) w += PP(PT_CPU)->c; if (PP(PT_GPU)) w += PP(PT_GPU)->c; return w; }
static int buildScore() {
  const Part *c = PP(PT_CPU), *g = PP(PT_GPU), *r = PP(PT_RAM), *s = PP(PT_SSD); if (!c) return 0;
  int sc = c->b * 30 + (g ? g->b * 55 : 0) + (r ? r->c * 30 + (r->b == 5 ? 300 : 0) : 0) + (s ? s->c / 8 : 0);
  return sc;
}
static float cpuTemp() {
  const Part *c = PP(PT_CPU), *k = PP(PT_COOL), *cs = PP(PT_CASE); if (!c || !k) return 120;
  float air = cs ? cs->a / 100.0f : 1.0f; return 32.0f + 62.0f * c->c / (k->c * air);
}
static int buildCost() { int s = 0; for (int t = 0; t < NPT; t++) if (S.slot[t] >= 0) s += PARTS[S.slot[t]].price; return s; }
// можно ли сейчас поставить деталь; если нет - причина
static bool canInstall(int pi, char *why, int n) {
  const Part &p = PARTS[pi]; why[0] = 0;
  if (S.slot[p.type] >= 0) { snprintf(why, n, "%s УЖЕ СТОИТ", PTN[p.type]); return false; }
  const Part *cs = PP(PT_CASE), *mb = PP(PT_MB);
  switch (p.type) {
    case PT_CASE: return true;
    case PT_MB: if (!cs) { snprintf(why, n, "СНАЧАЛА ПОСТАВЬ КОРПУС"); return false; } if (cs->c == 1 && p.c == 0) { snprintf(why, n, "ПЛАТА ATX НЕ ЛЕЗЕТ В МИНИ-КОРПУС"); return false; } return true;
    case PT_PSU: if (!cs) { snprintf(why, n, "СНАЧАЛА ПОСТАВЬ КОРПУС"); return false; } return true;
    case PT_CPU: if (!mb) { snprintf(why, n, "СНАЧАЛА ПОСТАВЬ ПЛАТУ"); return false; } if (mb->a != p.a) { snprintf(why, n, "НЕ ТОТ СОКЕТ: ПЛАТА %s, ПРОЦЕССОР %s", mb->a ? "Б" : "А", p.a ? "Б" : "А"); return false; } return true;
    case PT_COOL: if (!PP(PT_CPU)) { snprintf(why, n, "СНАЧАЛА ПОСТАВЬ ПРОЦЕССОР"); return false; } return true;
    case PT_RAM: if (!mb) { snprintf(why, n, "СНАЧАЛА ПОСТАВЬ ПЛАТУ"); return false; } if (mb->b != p.b) { snprintf(why, n, "ПЛАТА ПОД DDR%d, А ПАМЯТЬ DDR%d", mb->b, p.b); return false; } return true;
    case PT_SSD: if (!mb) { snprintf(why, n, "СНАЧАЛА ПОСТАВЬ ПЛАТУ"); return false; } return true;
    case PT_GPU: if (!mb) { snprintf(why, n, "СНАЧАЛА ПОСТАВЬ ПЛАТУ"); return false; } if (cs && p.d > cs->d) { snprintf(why, n, "ВИДЕОКАРТА %d ММ, А В КОРПУС ВЛЕЗАЕТ %d", p.d, cs->d); return false; } return true;
  }
  return true;
}
// что зависит от детали: снимаем вместе с ней
static void uninstall(int t) {
  if (S.slot[t] < 0) return;
  if (t == PT_CASE) { for (int k = 0; k < NPT; k++) if (k != PT_CASE) uninstall(k); }
  if (t == PT_MB) { uninstall(PT_CPU); uninstall(PT_RAM); uninstall(PT_SSD); uninstall(PT_GPU); S.cab[0] = 0; }
  if (t == PT_CPU) { uninstall(PT_COOL); S.cab[1] = 0; }
  if (t == PT_GPU) S.cab[2] = 0;
  if (t == PT_PSU) { S.cab[0] = S.cab[1] = S.cab[2] = 0; }
  S.inv[S.slot[t]]++; S.slot[t] = -1;
}
// требования заказа: сколько выполнено, строки для показа
struct Req { char text[64]; bool ok; };
static int orderReqs(const Order &o, Req *r, int score, bool tested) {
  const OrderType &t = OTYPES[o.type]; int n = 0; char b[64];
  snprintf(r[n].text, 64, "ОЦЕНКА НЕ МЕНЬШЕ %d", t.minScore); r[n++].ok = tested && score >= t.minScore;
  if (t.needGpu) { snprintf(r[n].text, 64, "ЕСТЬ ВИДЕОКАРТА"); r[n++].ok = PP(PT_GPU) != NULL; }
  snprintf(r[n].text, 64, "ПАМЯТЬ ОТ %d ГБ", t.minRam); r[n++].ok = PP(PT_RAM) && PP(PT_RAM)->c >= t.minRam;
  if (t.minSsd >= 1024) snprintf(b, sizeof b, "SSD ОТ %d ТБ", t.minSsd / 1024); else snprintf(b, sizeof b, "SSD ОТ %d ГБ", t.minSsd); snprintf(r[n].text, 64, "%s", b); r[n++].ok = PP(PT_SSD) && PP(PT_SSD)->c >= t.minSsd;
  if (t.mini) { snprintf(r[n].text, 64, "МАЛЕНЬКИЙ КОРПУС"); r[n++].ok = PP(PT_CASE) && PP(PT_CASE)->c == 1; }
  if (t.rgb) { snprintf(r[n].text, 64, "С ПОДСВЕТКОЙ RGB"); r[n++].ok = hasRgb(); }
  if (t.minCpu) { snprintf(r[n].text, 64, "ПРОЦЕССОР МОЩНОСТЬЮ ОТ %d", t.minCpu); r[n++].ok = PP(PT_CPU) && PP(PT_CPU)->b >= t.minCpu; }
  return n;
}

// =====================================================================
//  СБОРКА ПК: 3D - корпус, детали, провода, вентиляторы
// =====================================================================
// 1 единица = 100 мм. Корпус стоит на столе, открытая боковая стенка смотрит на +X.
static float fanSpin = 0, fanSpeed = 3.0f;          // вращение вентиляторов (на тесте - быстрее)
static float partAnim[NPT];                          // 0..1 - деталь «влетает» на место
static float cabAnim[3];
static bool gPowerOn = false;                        // ПК включён: светятся вентиляторы и подсветка
static V3 OFF;                                       // смещение текущей детали (анимация установки)
static void BXo(float x, float y, float z, float hx, float hy, float hz, u32 col) { drawBox(v3(x + OFF.x, y + OFF.y, z + OFF.z), v3(hx, hy, hz), 0, col, true); }
static void BXe(float x, float y, float z, float hx, float hy, float hz, u32 col) { drawBox(v3(x + OFF.x, y + OFF.y, z + OFF.z), v3(hx, hy, hz), 0, col, false); }   // светится - без тени
static u32 rgbCycle(float ph) { float t = gTime * 0.8f + ph; return RGB((int)(127 + 127 * sinf(t)), (int)(127 + 127 * sinf(t + 2.094f)), (int)(127 + 127 * sinf(t + 4.189f))); }
static float easeOut(float t) { t = clampf(t, 0, 1); return 1 - (1 - t) * (1 - t) * (1 - t); }
static void setOff(float a) { float k = 1 - easeOut(a); OFF = v3(3.5f * k, 1.6f * k, 0.6f * k); }
// вентилятор: рамка и 4 лопасти, крутятся (ось 0 - X, 1 - Y, 2 - Z)
static void drawFan(float x, float y, float z, int axis, float r, u32 frame, u32 blade, bool glow, float ph) {
  float t = 0.03f, ang = fanSpin + ph;
  if (axis == 0) { BXo(x, y + r, z, t, t, r, frame); BXo(x, y - r, z, t, t, r, frame); BXo(x, y, z + r, t, r, t, frame); BXo(x, y, z - r, t, r, t, frame); }
  if (axis == 1) { BXo(x + r, y, z, t, t, r, frame); BXo(x - r, y, z, t, t, r, frame); BXo(x, y, z + r, r, t, t, frame); BXo(x, y, z - r, r, t, t, frame); }
  if (axis == 2) { BXo(x, y + r, z, r, t, t, frame); BXo(x, y - r, z, r, t, t, frame); BXo(x + r, y, z, t, r, t, frame); BXo(x - r, y, z, t, r, t, frame); }
  for (int k = 0; k < 4; k++) { float a = ang + k * 1.5708f, ca = cosf(a) * r * 0.5f, sa = sinf(a) * r * 0.5f, b = r * 0.22f;
    if (axis == 0) BXo(x, y + ca, z + sa, 0.015f, b, b, blade);
    if (axis == 1) BXo(x + ca, y, z + sa, b, 0.015f, b, blade);
    if (axis == 2) BXo(x + ca, y + sa, z, b, b, 0.015f, blade); }
  float hb = r * 0.18f; if (axis == 0) BXo(x, y, z, 0.02f, hb, hb, frame); if (axis == 1) BXo(x, y, z, hb, 0.02f, hb, frame); if (axis == 2) BXo(x, y, z, hb, hb, 0.02f, frame);
  if (glow && gPowerOn) { u32 g = rgbCycle(ph); float e = r * 0.9f;                                            // светящееся кольцо
    if (axis == 2) { BXe(x, y + e, z + 0.01f, e, 0.012f, 0.012f, g); BXe(x, y - e, z + 0.01f, e, 0.012f, 0.012f, g); }
    if (axis == 1) { BXe(x + e, y - 0.01f, z, 0.012f, 0.012f, e, g); BXe(x - e, y - 0.01f, z, 0.012f, 0.012f, e, g); } }
}
static float caseFront() { const Part *c = PP(PT_CASE); return c && c->c == 1 ? 1.55f : 2.25f; }
static void drawCase(const Part &p) {
  u32 c = p.col, d = mixc(c, RGB(110, 115, 125), 0.4f); float fz = caseFront(), mz = (fz - 2.25f) * 0.5f, hz = (fz + 2.25f) * 0.5f;
  BXo(-1.0f, 2.35f, mz, 0.05f, 2.35f, hz, d);                       // стенка под плату
  BXo(0, 4.65f, mz, 1.05f, 0.05f, hz, c); BXo(0, 0.05f, mz, 1.05f, 0.05f, hz, c);   // верх и низ
  BXo(0, 2.35f, fz, 1.05f, 2.35f, 0.05f, c);                        // перед
  BXo(0, 2.35f, -2.25f, 1.05f, 2.35f, 0.05f, c);                    // зад
  BXo(0, 1.05f, mz, 1.0f, 0.04f, hz - 0.05f, mixc(c, RGB(90, 95, 105), 0.3f));     // кожух блока питания
  BXo(1.04f, 2.35f, fz - 0.02f, 0.02f, 2.3f, 0.04f, shadec(c, 1.15f)); BXo(1.04f, 4.6f, mz, 0.02f, 0.04f, hz, shadec(c, 1.15f));   // рамка открытого бока
  for (int k = 0; k < 4; k++) BXo(-0.35f, 4.2f - k * 0.25f, -2.2f, 0.45f, 0.06f, 0.02f, RGB(25, 25, 28));      // задняя решётка
  if (p.a >= 120) for (int r = 0; r < 7; r++) for (int q = 0; q < 4; q++) BXo(-0.75f + q * 0.5f, 0.6f + r * 0.6f, fz + 0.06f, 0.18f, 0.18f, 0.01f, shadec(c, 0.7f));   // сетка спереди
  drawFan(-0.3f, 3.9f, -2.15f, 2, 0.48f, RGB(30, 30, 34), RGB(70, 70, 78), p.b != 0, 0.3f);                     // задний вентилятор
  int nf = p.c == 1 ? 1 : (p.a >= 115 ? 3 : 2); for (int k = 0; k < nf; k++) drawFan(-0.2f, 3.6f - k * 1.15f, fz - 0.12f, 2, 0.5f, RGB(30, 30, 34), RGB(70, 70, 78), p.b != 0, k * 1.1f);
  if (p.b) { u32 g = gPowerOn ? rgbCycle(0) : RGB(60, 60, 70); BXe(1.0f, 2.35f, fz + 0.03f, 0.03f, 2.2f, 0.03f, g); }        // светодиодная полоса
  BXo(0.6f, 4.72f, fz - 0.4f, 0.12f, 0.03f, 0.12f, gPowerOn ? RGB(80, 220, 255) : RGB(70, 70, 76));             // кнопка включения
}
static float mbTop() { return 4.37f; }
static void drawMB(const Part &p) {
  bool mini = p.c == 1; float hy = mini ? 1.22f : 1.52f, cy = mbTop() - hy;
  BXo(-0.93f, cy, -0.95f, 0.03f, hy, 1.22f, p.col);
  BXo(-0.9f, 3.75f, -1.0f, 0.02f, 0.29f, 0.29f, RGB(200, 200, 205));                                            // сокет
  for (int k = 0; k < 4; k++) BXo(-0.89f, 3.6f, -0.37f + k * 0.08f, 0.02f, 0.66f, 0.022f, k & 1 ? RGB(90, 90, 96) : RGB(60, 60, 66));   // слоты памяти
  BXo(-0.89f, 2.62f, -1.35f, 0.025f, 0.04f, 0.85f, RGB(70, 70, 76)); if (!mini) BXo(-0.89f, 1.9f, -1.35f, 0.025f, 0.04f, 0.85f, RGB(70, 70, 76));   // слоты видеокарты
  BXo(-0.85f, 4.0f, -2.0f, 0.08f, 0.3f, 0.15f, RGB(60, 60, 66)); BXo(-0.85f, 3.2f, -1.95f, 0.08f, 0.35f, 0.12f, RGB(60, 60, 66));            // радиаторы питания
  BXo(-0.88f, 2.2f, -0.45f, 0.06f, 0.25f, 0.25f, RGB(80, 80, 88));                                               // чипсет
  BXo(-0.88f, 2.75f, 0.2f, 0.04f, 0.25f, 0.04f, RGB(230, 230, 230));                                             // разъём 24 pin
}
static void drawCPU(const Part &p) { BXo(-0.87f, 3.75f, -1.0f, 0.02f, 0.21f, 0.21f, RGB(185, 180, 165)); BXo(-0.855f, 3.75f, -1.0f, 0.005f, 0.12f, 0.12f, p.col); }
static void drawCooler(const Part &p) {
  u32 c = p.col; bool rgb = p.b != 0;
  switch (p.a) {
    case 0: BXo(-0.75f, 3.75f, -1.0f, 0.12f, 0.36f, 0.36f, RGB(170, 170, 175)); drawFan(-0.6f, 3.75f, -1.0f, 0, 0.32f, c, RGB(70, 70, 76), false, 0); break;
    case 1: case 2: { int nt = p.a == 2 ? 2 : 1;
      for (int t = 0; t < nt; t++) { float z = nt == 2 ? -1.32f + t * 0.64f : -1.0f;
        BXo(-0.42f, 3.8f, z, 0.45f, 0.55f, 0.2f, RGB(190, 195, 200)); for (int f = 0; f < 6; f++) BXo(-0.42f, 3.35f + f * 0.18f, z, 0.46f, 0.01f, 0.21f, RGB(150, 155, 160));
        for (int h = 0; h < 3; h++) BXo(-0.42f, 4.38f, z - 0.1f + h * 0.1f, 0.02f, 0.04f, 0.02f, RGB(200, 140, 90)); }   // тепловые трубки
      drawFan(-0.42f, 3.8f, nt == 2 ? -1.0f : -0.75f, 2, 0.48f, c, nt == 2 ? RGB(40, 40, 44) : RGB(70, 110, 180), rgb, 0.5f); } break;
    default: { int nf = p.a == 4 ? 3 : 2; float len = nf * 0.6f, zs = -2.0f;                                    // водянка: помпа и радиатор под крышей
      BXo(-0.78f, 3.75f, -1.0f, 0.1f, 0.27f, 0.27f, c); BXe(-0.67f, 3.75f, -1.0f, 0.01f, 0.15f, 0.15f, gPowerOn ? rgbCycle(1) : RGB(60, 60, 70));
      BXo(-0.2f, 4.45f, zs + len * 0.5f, 0.6f, 0.1f, len * 0.5f + 0.05f, RGB(35, 35, 40));
      for (int f = 0; f < nf; f++) drawFan(-0.2f, 4.3f, zs + 0.3f + f * 0.6f, 1, 0.27f, c, RGB(60, 60, 66), rgb, f * 0.7f);
      BXo(-0.65f, 4.05f, -1.15f, 0.04f, 0.3f, 0.04f, RGB(30, 30, 34)); BXo(-0.6f, 4.05f, -0.85f, 0.04f, 0.3f, 0.04f, RGB(30, 30, 34)); } break;   // шланги
  }
}
static void drawRAM(const Part &p) {
  bool rgb = strstr(p.name, "RGB") != NULL;
  for (int k = 0; k < 2; k++) { float z = -0.33f + k * 0.16f; BXo(-0.76f, 3.6f, z, 0.15f, 0.64f, 0.025f, p.col); BXo(-0.62f, 3.6f, z, 0.01f, 0.5f, 0.027f, shadec(p.col, 1.3f));
    if (rgb) BXe(-0.6f, 3.6f, z, 0.02f, 0.62f, 0.028f, gPowerOn ? rgbCycle(k * 0.5f) : RGB(200, 200, 210)); }
}
static void drawSSD(const Part &p) { BXo(-0.89f, 3.02f, -1.0f, 0.012f, 0.1f, 0.42f, RGB(20, 80, 40)); BXo(-0.875f, 3.02f, -1.05f, 0.006f, 0.08f, 0.32f, p.col); BXo(-0.87f, 3.02f, -0.75f, 0.006f, 0.07f, 0.04f, RGB(220, 220, 225)); }
static void drawGPU(const Part &p) {
  float L = p.d / 100.0f, z0 = -2.2f, zc = z0 + L * 0.5f; u32 c = p.col;
  BXo(-0.3f, 2.45f, zc, 0.62f, 0.22f, L * 0.5f, c);                                                                // кожух
  BXo(-0.3f, 2.69f, zc, 0.6f, 0.02f, L * 0.5f - 0.02f, RGB(30, 30, 34));                                            // бэкплейт
  BXo(-0.89f, 2.6f, z0 + 0.95f, 0.04f, 0.04f, 0.8f, RGB(200, 170, 70));                                             // разъём в слоте
  int nf = L > 2.8f ? 3 : (L > 2.0f ? 2 : 1); for (int f = 0; f < nf; f++) drawFan(-0.3f, 2.2f, z0 + L * (f + 0.5f) / nf, 1, fminv(0.5f, L / nf * 0.42f), RGB(25, 25, 28), RGB(60, 60, 66), false, f * 0.9f);
  BXe(0.33f, 2.5f, zc, 0.01f, 0.05f, L * 0.4f, gPowerOn ? (p.price > 40000 ? rgbCycle(2) : RGB(120, 220, 120)) : shadec(c, 1.4f));   // светящаяся надпись
  BXo(-0.3f, 2.45f, -2.24f, 0.62f, 0.22f, 0.02f, RGB(170, 170, 175));                                               // планка с выходами
}
static void drawPSU(const Part &p) {
  BXo(0.0f, 0.55f, -1.15f, 0.85f, 0.45f, 1.05f, p.col);
  BXo(0.86f, 0.55f, -1.15f, 0.01f, 0.25f, 0.6f, RGB(220, 220, 225));                                                // наклейка
  for (int k = 0; k < 5; k++) BXo(0.0f, 0.55f - 0.32f + k * 0.16f, -2.21f, 0.6f, 0.03f, 0.01f, RGB(15, 15, 18));   // решётка сзади
}
static void drawCable(int k) {
  u32 a = RGB(20, 20, 22), b = RGB(230, 230, 232);
  if (k == 0) { for (int s = 0; s < 6; s++) BXo(-0.75f, 1.75f + s * 0.17f, 0.42f, 0.09f, 0.085f, 0.07f, s & 1 ? a : b); BXo(-0.82f, 2.75f, 0.32f, 0.06f, 0.2f, 0.07f, a); }   // 24 pin
  if (k == 1) { BXo(-0.75f, 4.48f, -1.6f, 0.07f, 0.06f, 0.55f, a); BXo(-0.8f, 4.35f, -2.05f, 0.05f, 0.1f, 0.08f, a); }   // процессор 8 pin
  if (k == 2) { const Part *g = PP(PT_GPU); float L = g ? g->d / 100.0f : 2.5f, z = -2.2f + L - 0.35f; for (int s = 0; s < 4; s++) BXo(-0.1f, 1.25f + s * 0.27f, z, 0.07f, 0.13f, 0.06f, s & 1 ? a : b); }   // видеокарта
}
// подсветка места, куда встанет деталь
static V3 slotPos(int t) {
  switch (t) { case PT_CASE: return v3(0, 2.3f, 0); case PT_MB: return v3(-0.9f, 2.9f, -0.95f); case PT_CPU: return v3(-0.85f, 3.75f, -1.0f); case PT_COOL: return v3(-0.5f, 3.8f, -1.0f);
    case PT_RAM: return v3(-0.75f, 3.6f, -0.25f); case PT_SSD: return v3(-0.85f, 3.02f, -1.0f); case PT_GPU: return v3(-0.3f, 2.45f, -0.9f); default: return v3(0, 0.55f, -1.15f); }
}
static void drawPC() {
  OFF = v3(0, 0, 0);
  const Part *cs = PP(PT_CASE);
  if (cs) { setOff(partAnim[PT_CASE]); drawCase(*cs); }
  if (PP(PT_PSU)) { setOff(partAnim[PT_PSU]); drawPSU(*PP(PT_PSU)); }
  if (PP(PT_MB)) { setOff(partAnim[PT_MB]); drawMB(*PP(PT_MB)); }
  if (PP(PT_CPU)) { setOff(partAnim[PT_CPU]); drawCPU(*PP(PT_CPU)); }
  if (PP(PT_SSD)) { setOff(partAnim[PT_SSD]); drawSSD(*PP(PT_SSD)); }
  if (PP(PT_RAM)) { setOff(partAnim[PT_RAM]); drawRAM(*PP(PT_RAM)); }
  if (PP(PT_COOL)) { setOff(partAnim[PT_COOL]); drawCooler(*PP(PT_COOL)); }
  if (PP(PT_GPU)) { setOff(partAnim[PT_GPU]); drawGPU(*PP(PT_GPU)); }
  for (int k = 0; k < 3; k++) if (S.cab[k]) { float a = easeOut(cabAnim[k]); OFF = v3(0, (1 - a) * -1.2f, 0); drawCable(k); }
  OFF = v3(0, 0, 0);
}
// стол и стена мастерской
static void drawRoom() {
  groundQuad(-30, -30, 30, 30, -0.02f, RGB(120, 85, 55));
  groundQuad(-3.0f, -3.4f, 3.0f, 3.6f, -0.01f, RGB(40, 44, 52));                    // коврик
  for (int k = 0; k < 12; k++) groundQuad(-30, -30 + k * 5.0f, 30, -29.85f + k * 5.0f, -0.015f, RGB(105, 72, 46));   // доски стола
  drawBox(v3(-2.6f, 0.06f, 3.0f), v3(0.6f, 0.06f, 0.25f), 0.3f, RGB(200, 60, 50), true);                            // отвёртка
  drawBox(v3(-2.0f, 0.04f, 3.2f), v3(0.3f, 0.04f, 0.06f), 0.3f, RGB(200, 200, 205), true);
  drawBox(v3(2.4f, 0.25f, 2.8f), v3(0.35f, 0.25f, 0.35f), 0, RGB(230, 230, 220), true); drawBox(v3(2.4f, 0.5f, 2.8f), v3(0.3f, 0.02f, 0.3f), 0, RGB(110, 70, 40), true);   // кружка
}

// =====================================================================
//  СБОРКА ПК: экраны - заказы, магазин, склад, верстак, тест
// =====================================================================
enum { GS_TITLE, GS_ORDERS, GS_SHOP, GS_STORE, GS_BENCH };
static int gs = GS_TITLE;
enum { SFX_CLICK, SFX_BUY, SFX_PART, SFX_DENY, SFX_ON, SFX_CASH, SFX_FAIL, SFX_CABLE };
static void sfx(int k);
static char msg[120]; static float msgT = 0;
static void say(const char *m, float t = 2.5f) { snprintf(msg, sizeof msg, "%s", m); msgT = t; }
static int topH() { return fs * 14; }
static int tabH() { return fs * 17; }

// ---- текст, который не влезает - уменьшаем ----
static void drawFit(int x, int y, const char *s, int maxw, int sc, u32 col, int al) { while (sc > 1 && textW(s, sc) > maxw) sc--; drawText(x, y, s, sc, col, al); }
static void panel(int x, int y, int w, int h, u32 c) { fillRect(x, y, w, h, shadec(c, 0.6f)); fillRect(x + 1, y + 1, w - 2, h - 2, c); fillRect(x + 1, y + 1, w - 2, imax(1, fs / 2 + 1), shadec(c, 1.25f)); }

// ---- верхняя полоса и вкладки ----
static void topBar() {
  char b[64], m[48]; fillRect(0, 0, IW, topH(), RGB(22, 26, 40)); fillRect(0, topH() - 1, IW, 1, RGB(60, 70, 100));
  fmtMoney(m, sizeof m, S.money); snprintf(b, sizeof b, "ДЕНЬГИ: %s", m); drawTextSh(fs * 4, fs * 4, b, fs, RGB(120, 230, 140), 0);
  int lv = repLevel(); snprintf(b, sizeof b, "РЕПУТАЦИЯ: УР. %d", lv); drawTextSh(IW / 2 - fs * 30, fs * 4, b, fs, RGB(255, 210, 120), 0);
  int bx = IW / 2 + fs * 76, bw = fs * 36; fillRect(bx, fs * 5, bw, fs * 4, RGB(10, 12, 20)); fillRect(bx, fs * 5, lv >= 5 ? bw : bw * (S.rep % 20) / 20, fs * 4, RGB(255, 190, 70));
  snprintf(b, sizeof b, "ДЕНЬ %d", S.day); drawTextSh(IW - fs * 4, fs * 4, b, fs, RGB(200, 210, 230), 2);
}
static void tabsBar() {
  static const char *TN[4] = {"ЗАКАЗЫ", "МАГАЗИН", "СКЛАД", "ВЕРСТАК"}; static const int TG[4] = {GS_ORDERS, GS_SHOP, GS_STORE, GS_BENCH};
  int h = tabH(), y = IH - h, w = IW / 4; fillRect(0, y, IW, h, RGB(18, 22, 34));
  for (int i = 0; i < 4; i++) { bool on = gs == TG[i]; int x = i * w;
    if (button(x + fs, y + fs, w - fs * 2, h - fs * 2, TN[i], true, on ? RGB(60, 110, 200) : RGB(42, 48, 70))) { gs = TG[i]; sfx(SFX_CLICK); }
    if (i == 0 && S.active < 0) fillCircle(x + w - fs * 5, y + fs * 4, fs * 2, RGB(255, 80, 60), 255); }
}
static void drawMsg() { if (msgT <= 0) return; int w = textW(msg, fs) + fs * 10; fillRectA(IW / 2 - w / 2, IH - tabH() - fs * 16, w, fs * 12, RGB(0, 0, 0), 200); drawText(IW / 2, IH - tabH() - fs * 13, msg, fs, RGB(255, 235, 170), 1); }
static void bgStripes() { for (int y = 0; y < IH; y += fs * 4) fillRect(0, y, IW, fs * 4, ((y / (fs * 4)) & 1) ? RGB(32, 38, 58) : RGB(30, 35, 54)); }

// =====================================================================
//  ЗАКАЗЫ
// =====================================================================
static void frameOrders() {
  bgStripes(); topBar(); char b[96], m[48]; Req rq[8];
  int y0 = topH() + fs * 4, cw = (IW - fs * 16) / 3, ch = IH - tabH() - y0 - fs * 33;
  drawTextSh(IW / 2, y0, S.active >= 0 ? "ЗАКАЗ В РАБОТЕ - СОБЕРИ ЕГО НА ВЕРСТАКЕ" : "ВЫБЕРИ ЗАКАЗ", fs, RGB(255, 230, 150), 1); y0 += fs * 11;
  for (int k = 0; k < NOFFER; k++) { Order &o = S.offer[k]; const OrderType &t = OTYPES[o.type]; int x = fs * 4 + k * (cw + fs * 4); bool act = S.active == k;
    panel(x, y0, cw, ch, act ? RGB(40, 70, 60) : RGB(40, 46, 70));
    drawFit(x + cw / 2, y0 + fs * 4, t.name, cw - fs * 6, fs + 1, RGB(255, 255, 255), 1);
    snprintf(b, sizeof b, "КЛИЕНТ: %s", CLIENTS[o.client]); drawFit(x + cw / 2, y0 + fs * 16, b, cw - fs * 6, fs, RGB(190, 200, 225), 1);
    fmtMoney(m, sizeof m, o.pay); snprintf(b, sizeof b, "ПЛАТИТ %s", m); drawFit(x + cw / 2, y0 + fs * 27, b, cw - fs * 6, fs + 1, RGB(120, 230, 140), 1);
    int n = orderReqs(o, rq, 0, false), yy = y0 + fs * 41; drawText(x + fs * 5, yy, "НУЖНО:", fs, RGB(255, 210, 120), 0); yy += fs * 10;
    for (int i = 0; i < n; i++) { drawFit(x + fs * 5, yy, rq[i].text, cw - fs * 10, imax(1, fs - (cw < fs * 120 ? 1 : 0)), RGB(225, 230, 240), 0); yy += fs * 9; }
    int bh = fs * 15, by = y0 + ch - bh - fs * 4;
    if (act) { if (button(x + fs * 4, by, cw - fs * 8, bh, "ОТКАЗАТЬСЯ", true, RGB(130, 60, 60))) { S.active = -1; newOrder(o); saveGame(); sfx(SFX_CLICK); say("ЗАКАЗ ОТМЕНЁН"); } }
    else if (button(x + fs * 4, by, cw - fs * 8, bh, S.active >= 0 ? "СНАЧАЛА СДАЙ ТЕКУЩИЙ" : "ВЗЯТЬ ЗАКАЗ", S.active < 0, RGB(60, 150, 70))) { S.active = k; saveGame(); sfx(SFX_CLICK); say("ЗАКАЗ ВЗЯТ! КУПИ ДЕТАЛИ В МАГАЗИНЕ", 3); } }
  int by = IH - tabH() - fs * 20, bw = fs * 110;
  if (button(IW / 2 - bw / 2, by, bw, fs * 15, "НОВЫЙ ДЕНЬ - ДРУГИЕ ЗАКАЗЫ", true, RGB(70, 80, 130))) { S.day++; for (int k = 0; k < NOFFER; k++) if (k != S.active) newOrder(S.offer[k]); saveGame(); sfx(SFX_CLICK); }
  snprintf(b, sizeof b, "СДАНО ЗАКАЗОВ: %d", S.delivered); drawText(fs * 4, by + fs * 4, b, fs, RGB(170, 180, 200), 0);
  tabsBar(); drawMsg();
}

// =====================================================================
//  МАГАЗИН
// =====================================================================
static int shopCat = PT_CPU; static float shopScroll = 0;
static void frameShop() {
  bgStripes(); topBar(); char b[96], m[48];
  int y0 = topH() + fs * 3, cw = (IW - fs * 6) / NPT;
  for (int t = 0; t < NPT; t++) if (button(fs * 3 + t * cw, y0, cw - fs, fs * 13, PTN[t], true, t == shopCat ? RGB(60, 110, 200) : RGB(45, 52, 78))) { shopCat = t; shopScroll = 0; sfx(SFX_CLICK); }
  int ly0 = y0 + fs * 16, ly1 = IH - tabH() - fs * 2, rh = fs * 21, n = 0, idx[MAXPART]; for (int i = 0; i < NPART; i++) if (PARTS[i].type == shopCat) idx[n++] = i;
  static int dragY = -1; static float dragS = 0;
  if (P.pressed && P.y > ly0 && P.y < ly1) { dragY = P.y; dragS = shopScroll; } if (P.down && dragY >= 0) shopScroll = dragS + (P.y - dragY); if (!P.down) dragY = -1;
  shopScroll = clampf(shopScroll, (float)imin(0, (ly1 - ly0) - n * rh), 0); bool moved = abs(P.cy - P.sy) > fs * 5;
  setClipRect(0, ly0, IW, ly1);
  for (int k = 0; k < n; k++) { int i = idx[k]; const Part &p = PARTS[i]; int y = ly0 + (int)shopScroll + k * rh; if (y + rh < ly0 || y > ly1) continue;
    bool open = p.tier <= repLevel(); panel(fs * 3, y, IW - fs * 6, rh - fs * 2, open ? RGB(40, 46, 68) : RGB(34, 36, 46));
    fillRect(fs * 6, y + fs * 3, fs * 12, rh - fs * 8, p.col); fillRect(fs * 8, y + fs * 5, fs * 8, fs * 2, shadec(p.col, 1.5f));
    drawFit(fs * 22, y + fs * 2, p.name, IW / 2 - fs * 24, fs, open ? RGB(255, 255, 255) : RGB(140, 145, 160), 0);
    partInfo(i, b, sizeof b); drawFit(fs * 22, y + fs * 11, b, IW / 2 + fs * 20, imax(1, fs - 1), RGB(180, 190, 215), 0);
    fmtMoney(m, sizeof m, p.price); drawText(IW - fs * 70, y + fs * 6, m, fs, RGB(120, 230, 140), 2);
    if (S.inv[i] > 0) { snprintf(b, sizeof b, "НА СКЛАДЕ: %d", S.inv[i]); drawText(IW - fs * 70, y + fs * 13, b, imax(1, fs - 1), RGB(200, 200, 140), 2); }
    int bw = fs * 58, bx = IW - fs * 6 - bw;
    if (!open) { snprintf(b, sizeof b, "НУЖЕН УРОВЕНЬ %d", p.tier); button(bx, y + fs * 3, bw, rh - fs * 8, b, false, RGB(60, 60, 70)); }
    else if (button(bx, y + fs * 3, bw, rh - fs * 8, "КУПИТЬ", S.money >= p.price, RGB(60, 150, 70)) && !moved) { S.money -= p.price; S.inv[i]++; saveGame(); sfx(SFX_BUY); snprintf(b, sizeof b, "КУПЛЕНО: %s", p.name); say(b, 1.5f); } }
  setClipRect(0, 0, IW, IH);
  if (S.active >= 0) { const OrderType &t = OTYPES[S.offer[S.active].type]; snprintf(b, sizeof b, "ЗАКАЗ: %s", t.name); drawText(IW - fs * 4, IH - tabH() - fs * 9, b, imax(1, fs - 1), RGB(255, 220, 140), 2); }
  tabsBar(); drawMsg();
}

// =====================================================================
//  СКЛАД
// =====================================================================
static float storeScroll = 0;
static void frameStore() {
  bgStripes(); topBar(); char b[96], m[48];
  int y0 = topH() + fs * 4; drawTextSh(IW / 2, y0, "ДЕТАЛИ НА СКЛАДЕ. ЛИШНЕЕ МОЖНО ПРОДАТЬ ЗА 60% ЦЕНЫ", fs, RGB(255, 230, 150), 1);
  int ly0 = y0 + fs * 12, ly1 = IH - tabH() - fs * 2, rh = fs * 17, n = 0, idx[MAXPART]; for (int i = 0; i < NPART; i++) if (S.inv[i] > 0) idx[n++] = i;
  if (!n) drawText(IW / 2, ly0 + fs * 30, "ПУСТО. КУПИ ДЕТАЛИ В МАГАЗИНЕ", fs + 1, RGB(170, 180, 200), 1);
  static int dragY = -1; static float dragS = 0;
  if (P.pressed && P.y > ly0 && P.y < ly1) { dragY = P.y; dragS = storeScroll; } if (P.down && dragY >= 0) storeScroll = dragS + (P.y - dragY); if (!P.down) dragY = -1;
  storeScroll = clampf(storeScroll, (float)imin(0, (ly1 - ly0) - n * rh), 0); bool moved = abs(P.cy - P.sy) > fs * 5;
  setClipRect(0, ly0, IW, ly1);
  for (int k = 0; k < n; k++) { int i = idx[k]; const Part &p = PARTS[i]; int y = ly0 + (int)storeScroll + k * rh; if (y + rh < ly0 || y > ly1) continue;
    panel(fs * 3, y, IW - fs * 6, rh - fs * 2, RGB(40, 46, 68)); fillRect(fs * 6, y + fs * 3, fs * 10, rh - fs * 8, p.col);
    snprintf(b, sizeof b, "%s: %s  x%d", PTN[p.type], p.name, S.inv[i]); drawFit(fs * 20, y + fs * 5, b, IW / 2, fs, RGB(255, 255, 255), 0);
    int sell = p.price * 6 / 10; fmtMoney(m, sizeof m, sell); snprintf(b, sizeof b, "ПРОДАТЬ ЗА %s", m); int bw = fs * 80;
    if (button(IW - fs * 6 - bw, y + fs * 2, bw, rh - fs * 6, b, true, RGB(140, 90, 50)) && !moved) { S.inv[i]--; S.money += sell; saveGame(); sfx(SFX_CASH); } }
  setClipRect(0, 0, IW, IH);
  tabsBar(); drawMsg();
}

// =====================================================================
//  ВЕРСТАК
// =====================================================================
static float camYaw = 1.3f, camPitch = 0.3f, camDist = 8.2f;
enum { TS_IDLE, TS_POST, TS_BENCH, TS_DONE, TS_FAIL };
static int testSt = TS_IDLE; static float testT = 0; static int testScore = 0; static float testTemp = 0; static char failMsg[96];
static bool removeMode = false; static float listScroll = 0;
static bool readyToPower(char *miss, int n) {
  miss[0] = 0; const int need[7] = {PT_CASE, PT_MB, PT_CPU, PT_RAM, PT_SSD, PT_PSU, -1};
  for (int i = 0; need[i] >= 0; i++) if (S.slot[need[i]] < 0) { snprintf(miss, n, "НЕ ХВАТАЕТ: %s", PTN[need[i]]); return false; }
  if (!S.cab[0]) { snprintf(miss, n, "ПОДКЛЮЧИ ПИТАНИЕ ПЛАТЫ"); return false; }
  if (!S.cab[1]) { snprintf(miss, n, "ПОДКЛЮЧИ ПИТАНИЕ ПРОЦЕССОРА"); return false; }
  if (PP(PT_GPU) && !S.cab[2]) { snprintf(miss, n, "ПОДКЛЮЧИ ПИТАНИЕ ВИДЕОКАРТЫ"); return false; }
  return true;
}
static void startTest() {
  testSt = TS_POST; testT = 0; gPowerOn = true; sfx(SFX_ON);
  testScore = buildScore(); testTemp = cpuTemp();
}
static void draw3DView(int x0, int y0, int x1, int y1) {
  for (int y = y0; y < y1; y++) fillRect(x0, y, x1 - x0, 1, mixc(RGB(70, 80, 105), RGB(150, 140, 125), (y - y0) / (float)imax(1, y1 - y0)));   // стена мастерской
  setClipRect(x0, y0, x1, y1); clearDepth(); gLight = 1.25f; gNight = 0; fogA = 1e9f; fogB = 2e9f; LDIR = vnorm(v3(0.85f, 0.5f, 0.3f));
  V3 tg = v3(-0.3f, 2.3f, -0.1f), eye = v3(tg.x + sinf(camYaw) * cosf(camPitch) * camDist, tg.y + sinf(camPitch) * camDist, tg.z + cosf(camYaw) * cosf(camPitch) * camDist);
  setCamera(eye, tg); focal = (y1 - y0) * 1.35f; vcx = (x0 + x1) * 0.5f; vcy = (y0 + y1) * 0.5f;
  drawRoom(); drawPC();
  setClipRect(0, 0, IW, IH);
}
static void markSlot(int t) {                                         // пульсирующий кружок там, куда ставить
  V3 p = slotPos(t); int sx, sy; float z; if (!projectPt(p, sx, sy, z)) return; float k = 0.5f + 0.5f * sinf(gTime * 6);
  fillCircle(sx, sy, (int)(fs * (6 + 3 * k)), RGB(120, 255, 160), (int)(70 + 60 * k)); fillCircle(sx, sy, fs * 2, RGB(255, 255, 255), 200);
}
static int nextNeeded() { const int ord[8] = {PT_CASE, PT_MB, PT_CPU, PT_COOL, PT_RAM, PT_SSD, PT_GPU, PT_PSU}; for (int i = 0; i < 8; i++) if (S.slot[ord[i]] < 0) { if (ord[i] == PT_GPU && S.active >= 0 && !OTYPES[S.offer[S.active].type].needGpu) continue; return ord[i]; } return -1; }
static void deliver() {
  Order &o = S.offer[S.active]; const OrderType &t = OTYPES[o.type]; int cost = buildCost(); bool great = testScore >= t.minScore * 5 / 4;
  int pay = o.pay + (great ? o.pay / 10 : 0); S.money += pay; S.rep = imin(200, S.rep + 6 + t.lvl * 2 + (great ? 3 : 0)); S.delivered++; S.best = imax(S.best, testScore);
  for (int k = 0; k < NPT; k++) S.slot[k] = -1; S.cab[0] = S.cab[1] = S.cab[2] = 0; S.active = -1; newOrder(o);
  char b[120], m1[48], m2[48]; fmtMoney(m1, sizeof m1, pay); fmtMoney(m2, sizeof m2, pay - cost);
  snprintf(b, sizeof b, "ЗАКАЗ СДАН! +%s (ПРИБЫЛЬ %s)%s", m1, m2, great ? " + ЧАЕВЫЕ" : ""); say(b, 4); sfx(SFX_CASH);
  testSt = TS_IDLE; gPowerOn = false; saveGame();
}
static void frameBench(float dt) {
  char b[120], m[48]; int vx1 = IW * 60 / 100, vy0 = topH(), vy1 = IH - tabH();
  // поворот камеры пальцем по 3D
  static int lastX = 0, lastY = 0; static bool rot = false;
  if (P.pressed && P.x < vx1 && P.y > vy0 && P.y < vy1) { rot = true; lastX = P.x; lastY = P.y; }
  if (rot && P.down) { camYaw = clampf(camYaw - (P.x - lastX) * 0.008f, -0.2f, 2.9f); camPitch = clampf(camPitch + (P.y - lastY) * 0.006f, 0.05f, 1.2f); lastX = P.x; lastY = P.y; }
  if (!P.down) rot = false;
  for (int t = 0; t < NPT; t++) partAnim[t] = fminv(1.0f, partAnim[t] + dt * 2.2f); for (int k = 0; k < 3; k++) cabAnim[k] = fminv(1.0f, cabAnim[k] + dt * 2.5f);
  fanSpeed += ((gPowerOn ? (testSt == TS_BENCH ? 30.0f : 12.0f) : 0.0f) - fanSpeed) * fminv(1.0f, dt * 2); fanSpin += fanSpeed * dt;
  draw3DView(0, vy0, vx1, vy1);
  int nn = nextNeeded(); if (testSt == TS_IDLE && nn >= 0) markSlot(nn);
  drawText(fs * 4, vy0 + fs * 4, "ВЕДИ ПАЛЬЦЕМ, ЧТОБЫ ПОВЕРНУТЬ", imax(1, fs - 1), RGB(230, 235, 245), 0);
  // ---- тест: загрузка, нагрузка, итог ----
  if (testSt != TS_IDLE) { testT += dt; int pw = vx1 - fs * 12, ph = fs * 80, px = fs * 6, py = vy1 - ph - fs * 4;
    fillRectA(px, py, pw, ph, RGB(0, 0, 10), 165);
    if (testSt == TS_POST) {
      if (testT > 0.6f && S.slot[PT_PSU] >= 0 && PARTS[S.slot[PT_PSU]].c < powerNeed()) { testSt = TS_FAIL; snprintf(failMsg, sizeof failMsg, "БЛОК ПИТАНИЯ НЕ ТЯНЕТ: НУЖНО %d ВТ, А ОН %d ВТ", powerNeed(), PARTS[S.slot[PT_PSU]].c); gPowerOn = false; sfx(SFX_FAIL); }
      const char *L[6] = {"BIOS: ПРОВЕРКА...", "ПРОЦЕССОР - OK", "ПАМЯТЬ - OK", "НАКОПИТЕЛЬ - OK", "ЗАГРУЗКА СИСТЕМЫ...", ""};
      for (int i = 0; i < 5 && i <= (int)(testT * 3); i++) drawText(px + fs * 6, py + fs * 6 + i * fs * 11, L[i], fs, RGB(170, 255, 170), 0);
      if (testSt == TS_POST && testT > 2.0f) { testSt = TS_BENCH; testT = 0; } }
    if (testSt == TS_BENCH) { float k = clampf(testT / 4.0f, 0, 1); float temp = 30 + (testTemp - 30) * fminv(1.0f, k * 1.4f);
      drawTextSh(px + pw / 2, py + fs * 6, "ТЕСТ ПОД НАГРУЗКОЙ", fs + 1, RGB(255, 255, 255), 1);
      fillRect(px + fs * 8, py + fs * 22, pw - fs * 16, fs * 6, RGB(30, 30, 40)); fillRect(px + fs * 8, py + fs * 22, (int)((pw - fs * 16) * k), fs * 6, RGB(80, 200, 255));
      snprintf(b, sizeof b, "ОЦЕНКА: %d", (int)(testScore * k)); drawTextSh(px + pw / 2, py + fs * 34, b, fs * 2, RGB(255, 230, 120), 1);
      snprintf(b, sizeof b, "ПРОЦЕССОР: %d C", (int)temp); drawText(px + pw / 2, py + fs * 56, b, fs + 1, temp > 90 ? RGB(255, 90, 80) : (temp > 80 ? RGB(255, 200, 80) : RGB(140, 255, 160)), 1);
      snprintf(b, sizeof b, "ПОТРЕБЛЕНИЕ: %d ВТ ИЗ %d", powerNeed(), PARTS[S.slot[PT_PSU]].c); drawText(px + pw / 2, py + fs * 68, b, fs, RGB(200, 210, 230), 1);
      if (temp > 100 && testTemp > 100) { testSt = TS_FAIL; snprintf(failMsg, sizeof failMsg, "%s", S.slot[PT_COOL] < 0 ? "НЕТ КУЛЕРА - ПРОЦЕССОР ПЕРЕГРЕЛСЯ!" : "КУЛЕР НЕ СПРАВЛЯЕТСЯ - ПЕРЕГРЕВ!"); gPowerOn = false; sfx(SFX_FAIL); }
      else if (testT > 4.0f) { testSt = TS_DONE; if (testTemp > 90) testScore = testScore * 3 / 4; sfx(SFX_CASH); } }
    if (testSt == TS_DONE) { drawTextSh(px + pw / 2, py + fs * 6, "ТЕСТ ПРОЙДЕН", fs + 1, RGB(140, 255, 160), 1);
      snprintf(b, sizeof b, "ОЦЕНКА: %d", testScore); drawTextSh(px + pw / 2, py + fs * 20, b, fs * 2, RGB(255, 230, 120), 1);
      snprintf(b, sizeof b, "ТЕМПЕРАТУРА %d C%s", (int)testTemp, testTemp > 90 ? " - ГРЕЕТСЯ, ОЦЕНКА НИЖЕ" : ""); drawText(px + pw / 2, py + fs * 40, b, fs, testTemp > 90 ? RGB(255, 160, 80) : RGB(200, 220, 240), 1);
      fmtMoney(m, sizeof m, buildCost()); snprintf(b, sizeof b, "ДЕТАЛИ СТОЯТ: %s", m); drawText(px + pw / 2, py + fs * 52, b, fs, RGB(200, 220, 240), 1); }
    if (testSt == TS_FAIL) { drawTextSh(px + pw / 2, py + fs * 10, "ПК ВЫКЛЮЧИЛСЯ!", fs * 2, RGB(255, 100, 90), 1); drawFit(px + pw / 2, py + fs * 34, failMsg, pw - fs * 8, fs, RGB(255, 220, 200), 1);
      drawText(px + pw / 2, py + fs * 48, "ПОМЕНЯЙ ДЕТАЛЬ И ВКЛЮЧИ СНОВА", fs, RGB(200, 210, 230), 1); } }
  // ---- правая панель ----
  int rx = vx1, rw = IW - vx1; fillRect(rx, vy0, rw, vy1 - vy0, RGB(28, 33, 50)); fillRect(rx, vy0, 1, vy1 - vy0, RGB(70, 80, 110));
  int y = vy0 + fs * 3; Req rq[8];
  if (S.active >= 0) { const Order &o = S.offer[S.active]; snprintf(b, sizeof b, "ЗАКАЗ: %s", OTYPES[o.type].name); drawFit(rx + fs * 4, y, b, rw - fs * 8, fs, RGB(255, 220, 140), 0); y += fs * 10;
    int n = orderReqs(o, rq, testSt == TS_DONE ? testScore : 0, testSt == TS_DONE);
    for (int i = 0; i < n; i++) { snprintf(b, sizeof b, "%s %s", rq[i].ok ? "+" : "-", rq[i].text); drawFit(rx + fs * 4, y, b, rw - fs * 8, imax(1, fs - 1), rq[i].ok ? RGB(140, 240, 150) : RGB(220, 170, 160), 0); y += fs * 8; }
    fmtMoney(m, sizeof m, buildCost()); char m2[48]; fmtMoney(m2, sizeof m2, o.pay); snprintf(b, sizeof b, "ДЕТАЛИ %s ИЗ %s", m, m2); drawFit(rx + fs * 4, y, b, rw - fs * 8, imax(1, fs - 1), buildCost() > o.pay ? RGB(255, 120, 100) : RGB(170, 200, 230), 0); y += fs * 10; }
  else { drawFit(rx + fs * 4, y, "СВОБОДНАЯ СБОРКА (ВОЗЬМИ ЗАКАЗ)", rw - fs * 8, fs, RGB(200, 210, 230), 0); y += fs * 12; }
  int bh = fs * 13, bw = rw - fs * 8, bx = rx + fs * 4;
  if (testSt == TS_DONE || testSt == TS_FAIL) {
    bool ok = false; if (testSt == TS_DONE && S.active >= 0) { ok = true; int n = orderReqs(S.offer[S.active], rq, testScore, true); for (int i = 0; i < n; i++) if (!rq[i].ok) ok = false; }
    if (S.active >= 0 && testSt == TS_DONE) { if (button(bx, y, bw, bh + fs * 4, ok ? "СДАТЬ ЗАКАЗ" : "НЕ ПОДХОДИТ ПОД ЗАКАЗ", ok, RGB(60, 160, 70))) { deliver(); return; } y += bh + fs * 7; }
    if (button(bx, y, bw, bh, "ВЫКЛЮЧИТЬ И ДОРАБОТАТЬ", true, RGB(70, 80, 130))) { testSt = TS_IDLE; gPowerOn = false; sfx(SFX_CLICK); } y += bh + fs * 3;
    tabsBar(); drawMsg(); return; }
  if (testSt != TS_IDLE) { tabsBar(); drawMsg(); return; }
  // кнопки режима
  int hw = (bw - fs * 2) / 2;
  if (button(bx, y, hw, fs * 11, "ПОСТАВИТЬ", true, !removeMode ? RGB(60, 110, 200) : RGB(45, 52, 78))) { removeMode = false; listScroll = 0; }
  if (button(bx + hw + fs * 2, y, hw, fs * 11, "СНЯТЬ", true, removeMode ? RGB(160, 80, 60) : RGB(45, 52, 78))) { removeMode = true; listScroll = 0; }
  y += fs * 14;
  int ly0 = y, ly1 = vy1 - fs * 38, rh = fs * 15;
  struct Row { int kind, id; bool ok; char why[96]; }; static Row rows[MAXPART + 8]; int nr = 0;
  if (!removeMode) {
    for (int k = 0; k < 3; k++) { bool can = !S.cab[k] && S.slot[PT_PSU] >= 0 && (k == 0 ? S.slot[PT_MB] >= 0 : (k == 1 ? S.slot[PT_CPU] >= 0 : S.slot[PT_GPU] >= 0)); if (can) { rows[nr].kind = 1; rows[nr].id = k; rows[nr].ok = true; rows[nr].why[0] = 0; nr++; } }
    for (int pass = 0; pass < 2; pass++) for (int i = 0; i < NPART; i++) if (S.inv[i] > 0) { char w[96]; bool ok = canInstall(i, w, sizeof w); if (ok != (pass == 0)) continue; rows[nr].kind = 0; rows[nr].id = i; rows[nr].ok = ok; snprintf(rows[nr].why, 96, "%s", w); nr++; }
  } else for (int t = 0; t < NPT; t++) if (S.slot[t] >= 0) { rows[nr].kind = 2; rows[nr].id = t; rows[nr].ok = true; rows[nr].why[0] = 0; nr++; }
  if (!nr) drawFit(bx, ly0 + fs * 4, removeMode ? "НИЧЕГО НЕ УСТАНОВЛЕНО" : "НА СКЛАДЕ ПУСТО - КУПИ ДЕТАЛИ", bw, imax(1, fs - 1), RGB(170, 180, 200), 0);
  static int dragY = -1; static float dragS = 0;
  if (P.pressed && P.x > rx && P.y > ly0 && P.y < ly1) { dragY = P.y; dragS = listScroll; } if (P.down && dragY >= 0) listScroll = dragS + (P.y - dragY); if (!P.down) dragY = -1;
  listScroll = clampf(listScroll, (float)imin(0, (ly1 - ly0) - nr * rh), 0); bool moved = abs(P.cy - P.sy) > fs * 5;
  setClipRect(rx, ly0, IW, ly1);
  for (int r = 0; r < nr; r++) { int yy = ly0 + (int)listScroll + r * rh; if (yy + rh < ly0 || yy > ly1) continue; Row &R = rows[r];
    if (R.kind == 1) { static const char *CN[3] = {"ПРОВОД: ПИТАНИЕ ПЛАТЫ", "ПРОВОД: ПИТАНИЕ ПРОЦЕССОРА", "ПРОВОД: ПИТАНИЕ ВИДЕОКАРТЫ"};
      if (button(bx, yy, bw, rh - fs * 2, CN[R.id], true, RGB(150, 120, 40)) && !moved) { S.cab[R.id] = 1; cabAnim[R.id] = 0; saveGame(); sfx(SFX_CABLE); } }
    else if (R.kind == 0) { if (S.inv[R.id] <= 0) continue; const Part &p = PARTS[R.id]; snprintf(b, sizeof b, "%s x%d", p.name, S.inv[R.id]);
      if (button(bx, yy, bw, rh - fs * 2, b, true, R.ok ? RGB(50, 120, 80) : RGB(70, 60, 64)) && !moved) {
        if (R.ok) { S.inv[R.id]--; S.slot[p.type] = R.id; partAnim[p.type] = 0; saveGame(); sfx(SFX_PART); } else { say(R.why, 3); sfx(SFX_DENY); } } }
    else { if (S.slot[R.id] < 0) continue; const Part &p = PARTS[S.slot[R.id]]; snprintf(b, sizeof b, "СНЯТЬ: %s", p.name);   /* могла сняться вместе с другой в этом кадре */
      if (button(bx, yy, bw, rh - fs * 2, b, true, RGB(140, 70, 60)) && !moved) { uninstall(R.id); saveGame(); sfx(SFX_CLICK); } } }
  setClipRect(0, 0, IW, IH);
  // подсказка и включение
  y = ly1 + fs * 3; char miss[96]; bool ready = readyToPower(miss, sizeof miss);
  if (!ready) { const char *hint = miss; char hb[96];
    if (nn >= 0 && !removeMode) { bool have = false; for (int i = 0; i < NPART; i++) if (S.inv[i] > 0 && PARTS[i].type == nn) have = true; snprintf(hb, sizeof hb, have ? "ПОСТАВЬ: %s" : "КУПИ В МАГАЗИНЕ: %s", PTN[nn]); hint = hb; }
    drawFit(bx, y, hint, bw, imax(1, fs - 1), RGB(255, 220, 140), 0); }
  else drawFit(bx, y, S.slot[PT_COOL] < 0 ? "БЕЗ КУЛЕРА ПК ПЕРЕГРЕЕТСЯ!" : "ВСЁ ГОТОВО - ВКЛЮЧАЙ!", bw, imax(1, fs - 1), S.slot[PT_COOL] < 0 ? RGB(255, 140, 110) : RGB(140, 240, 150), 0);
  y += fs * 10;
  if (button(bx, y, bw, fs * 18, "ВКЛЮЧИТЬ", ready, RGB(60, 160, 70))) startTest();
  tabsBar(); drawMsg();
}

// =====================================================================
//  ЗАСТАВКА: крутится собранный топовый ПК
// =====================================================================
static void frameTitle(float dt) {
  int saved[NPT], cab[3]; for (int t = 0; t < NPT; t++) { saved[t] = S.slot[t]; partAnim[t] = 1; } for (int k = 0; k < 3; k++) { cab[k] = S.cab[k]; S.cab[k] = 1; cabAnim[k] = 1; }
  const char *demo[NPT] = {"БАШНЯ RGB", "ПЛАТА Б-790 ЭКСТРИМ", "ВЕКТОР В9", "ВОДЯНКА 360 RGB", "DDR5 64 ГБ RGB", "SSD 2 ТБ", "ГРОМ 4090 ТИТАН", "БП 1300 ВТ"};
  for (int t = 0; t < NPT; t++) for (int i = 0; i < NPART; i++) if (!strcmp(PARTS[i].name, demo[t])) S.slot[t] = i;
  bool po = gPowerOn; gPowerOn = true; fanSpeed = 12; fanSpin += dt * 12; float cy = camYaw, cp = camPitch; camYaw = 1.15f + sinf(gTime * 0.3f) * 0.55f; camPitch = 0.28f; float cd = camDist; camDist = 9.5f;
  draw3DView(0, 0, IW, IH);
  camYaw = cy; camPitch = cp; camDist = cd; gPowerOn = po; for (int t = 0; t < NPT; t++) S.slot[t] = saved[t]; for (int k = 0; k < 3; k++) S.cab[k] = cab[k];
  fillRectA(0, 0, IW, IH / 4, RGB(0, 0, 0), 120);
  drawTextSh(IW / 2, fs * 8, "СБОРКА ПК", fs * 4, RGB(120, 220, 255), 1);
  drawText(IW / 2, fs * 40, "МАСТЕРСКАЯ КОМПЬЮТЕРОВ: ЗАКАЗЫ, ДЕТАЛИ, СБОРКА, ТЕСТ", fs, RGB(230, 235, 245), 1);
  int bw = fs * 90, bh = fs * 22;
  if (button(IW / 2 - bw / 2, IH - bh - fs * 30, bw, bh, "ИГРАТЬ", true, RGB(60, 160, 70))) { gs = S.active >= 0 ? GS_BENCH : GS_ORDERS; sfx(SFX_CLICK); }
  if (button(IW / 2 - bw / 2, IH - fs * 24, bw, fs * 16, S.soundOn ? "ЗВУК: ВКЛ" : "ЗВУК: ВЫКЛ", true, RGB(70, 80, 130))) { S.soundOn = !S.soundOn; saveGame(); }
}

// =====================================================================
//  СБОРКА ПК: звук, ввод, главный цикл
// =====================================================================
struct Voice { float f0, f1, t, dur, vol, ph; int noise; };
static Voice voices[10]; static volatile int vHead = 0; static bool audioOk = false;
static void audioCb(void *, Uint8 *st, int len) {
  short *o = (short *)st; int n = len / 2; static unsigned rs = 777u;
  for (int i = 0; i < n; i++) { float v = 0;
    for (int k = 0; k < 10; k++) { Voice &vc = voices[k]; if (vc.dur <= 0 || vc.t >= vc.dur) continue; float k01 = vc.t / vc.dur, env = (1 - k01) * (1 - k01) * vc.vol, f = vc.f0 + (vc.f1 - vc.f0) * k01;
      if (vc.noise) { rs = rs * 1664525u + 1013904223u; float nz = ((rs >> 9) & 0xFFFF) / 32768.0f - 1.0f; vc.ph += (nz - vc.ph) * 0.3f; v += vc.ph * env; }
      else { vc.ph += f / 22050.0f; if (vc.ph > 1) vc.ph -= 1; v += sinf(vc.ph * 6.2832f) * 0.6f * env + (vc.ph < 0.5f ? 0.2f : -0.2f) * env; }
      vc.t += 1.0f / 22050.0f; }
    if (v > 1) v = 1; if (v < -1) v = -1; o[i] = (short)(v * 13000); }
}
static void audioInit() {
  if (audioOk) return; SDL_InitSubSystem(SDL_INIT_AUDIO);
  SDL_AudioSpec want; memset(&want, 0, sizeof want); want.freq = 22050; want.format = AUDIO_S16SYS; want.channels = 1; want.samples = 1024; want.callback = audioCb;
  if (SDL_OpenAudio(&want, NULL) == 0) { audioOk = true; SDL_PauseAudio(0); }
}
static void voice(float f0, float f1, float dur, float vol, int noise) { if (!audioOk || !S.soundOn) return; int k = vHead; vHead = (vHead + 1) % 10; Voice v; v.f0 = f0; v.f1 = f1; v.t = 0; v.dur = dur; v.vol = vol; v.ph = 0; v.noise = noise; voices[k] = v; }
static void sfx(int k) {
  switch (k) {
    case SFX_CLICK: voice(900, 700, 0.05f, 0.3f, 0); break;
    case SFX_BUY: voice(700, 1100, 0.12f, 0.35f, 0); break;
    case SFX_PART: voice(0, 0, 0.08f, 0.45f, 1); voice(220, 160, 0.1f, 0.35f, 0); break;       // щелчок защёлки
    case SFX_CABLE: voice(0, 0, 0.05f, 0.4f, 1); voice(1200, 900, 0.04f, 0.2f, 0); break;
    case SFX_DENY: voice(220, 180, 0.15f, 0.4f, 0); break;
    case SFX_ON: voice(1000, 1000, 0.15f, 0.35f, 0); break;                                         // «пик» при включении
    case SFX_CASH: voice(800, 800, 0.08f, 0.35f, 0); voice(1200, 1600, 0.25f, 0.3f, 0); break;
    case SFX_FAIL: voice(300, 100, 0.5f, 0.5f, 0); voice(0, 0, 0.3f, 0.3f, 1); break;
  }
}
// ---- ввод: один палец ----
static long long ptrId = -99;
static void fingerDown(long long id, int x, int y) { if (P.down) return; ptrId = id; P.down = true; P.pressed = true; P.x = P.sx = x; P.y = P.sy = y; }
static void fingerMove(long long id, int x, int y) { if (!P.down || id != ptrId) return; P.dx += x - P.x; P.dy += y - P.y; P.x = x; P.y = y; }
static void fingerUp(long long id, int x, int y) { if (!P.down || id != ptrId) return; P.down = false; P.x = x; P.y = y; P.cx = x; P.cy = y; P.clicked = true; }
static void goBack(bool &running) { if (gs == GS_TITLE) running = false; else if (gs == GS_BENCH && testSt != TS_IDLE) { testSt = TS_IDLE; gPowerOn = false; } else gs = GS_TITLE; saveGame(); }
static float fpsShow = 0, fpsAcc = 0; static int fpsN = 0;

int main(int argc, char **argv) {
  (void)argc; (void)argv;
#if defined(__ANDROID__) && defined(SDL_MAJOR_VERSION) && SDL_MAJOR_VERSION >= 2
  { const char *ip = SDL_AndroidGetInternalStoragePath(); if (ip && *ip) snprintf(gBase, sizeof gBase, "%s/", ip); }
#endif
  SDL_SetHint("SDL_ANDROID_TRAP_BACK_BUTTON", "1");
  if (!gfxInit()) return 1;
  rotMode = false; LW = SW; LH = SH;
  computeSize(); if (!allocBuffers()) return 1;
  srand((unsigned)time(NULL));
  if (!loadGame()) { FILE *old = fopen(pth("sborka.sav"), "rb"); if (old) { fclose(old); rename(pth("sborka.sav"), pth("sborka.broken")); } newGame(); saveGame(); }
  for (int t = 0; t < NPT; t++) partAnim[t] = 1; for (int k = 0; k < 3; k++) cabAnim[k] = 1;
  audioInit();
  memset(&P, 0, sizeof P);
  bool running = true; Uint32 last = SDL_GetTicks();
  while (running) {
    Uint32 now = SDL_GetTicks();
    float dt = clampf((now - last) / 1000.0f, 0.001f, 0.05f); last = now; gTime += dt;
    if (gfxCheckResize()) { rotMode = false; LW = SW; LH = SH; computeSize(); if (!allocBuffers()) break; }
    P.pressed = false; P.clicked = false; P.dx = P.dy = 0;
#if SDL_MODE == 2
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
      int mx, my;
      switch (ev.type) {
        case SDL_QUIT: running = false; break;
        case SDL_APP_WILLENTERBACKGROUND: case SDL_APP_TERMINATING: case SDL_APP_LOWMEMORY: saveGame(); break;
        case SDL_KEYDOWN: if (ev.key.keysym.sym == SDLK_ESCAPE
#if defined(__ANDROID__)
          || ev.key.keysym.sym == SDLK_AC_BACK
#endif
          ) goBack(running); break;
        case SDL_FINGERDOWN: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerDown((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERMOTION: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerMove((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_FINGERUP: mapPt((int)(ev.tfinger.x * SW), (int)(ev.tfinger.y * SH), mx, my); fingerUp((long long)ev.tfinger.fingerId, mx, my); break;
        case SDL_MOUSEBUTTONDOWN: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerDown(-1, mx, my); } break;
        case SDL_MOUSEMOTION: if (ev.motion.which != SDL_TOUCH_MOUSEID) { mapPt(ev.motion.x, ev.motion.y, mx, my); fingerMove(-1, mx, my); } break;
        case SDL_MOUSEBUTTONUP: if (ev.button.which != SDL_TOUCH_MOUSEID) { mapPt(ev.button.x, ev.button.y, mx, my); fingerUp(-1, mx, my); } break;
        default: break;
      }
    }
#else
    Ev e;
    while (pollEv(e)) {
      if (e.type == 0) running = false;
      else if (e.type == 4) goBack(running);
      else if (e.type == 5) saveGame();
      else { int mx, my; mapPt(e.x, e.y, mx, my); if (e.type == 1) fingerDown(-1, mx, my); else if (e.type == 3) fingerMove(-1, mx, my); else fingerUp(-1, mx, my); }
    }
#endif
    if (msgT > 0) msgT -= dt;
    switch (gs) { case GS_TITLE: frameTitle(dt); break; case GS_ORDERS: frameOrders(); break; case GS_SHOP: frameShop(); break; case GS_STORE: frameStore(); break; default: frameBench(dt); break; }
    if (fpsOn) { char b[16]; snprintf(b, sizeof b, "%d", (int)(fpsShow + 0.5f)); drawText(IW - fs * 2, IH - fs * 9, b, imax(1, fs - 1), RGB(150, 160, 180), 2); }
    gfxPresent();
    Uint32 spent = SDL_GetTicks() - now; fpsAcc += dt; fpsN++; if (fpsAcc >= 1.0f) { fpsShow = fpsN / fpsAcc; fpsAcc = 0; fpsN = 0; }
    if (spent < 16) SDL_Delay(16 - spent);
  }
  saveGame(); gfxQuit();
  return 0;
}
