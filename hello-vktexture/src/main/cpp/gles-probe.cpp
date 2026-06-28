// GLES (OpenGL ES 3.0) R8 texture integrity probe — the API path Chromium's
// renderer actually uses. Chromium's Skia Ganesh GL backend issues GLES calls
// that Berberis marshals through the GLES proxy to host ANGLE (GLES->Vulkan) ->
// gfxstream -> Mesa. This is a DIFFERENT proxy path than direct Vulkan (which
// the hello-vktexture Vulkan probe covers), so it must be tested separately.
//
// Each test uploads a deterministic pattern into a GL_R8 texture (full image and
// incremental sub-rectangles via glTexSubImage2D — Chromium's per-glyph atlas
// placement), samples it NEAREST 1:1 onto an RGBA8 FBO, reads the result back,
// and checks each output red channel equals the source texel.

#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <android/log.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define LOG_TAG "hellovktexture"

namespace {

const char* kVS =
    "#version 300 es\n"
    "out vec2 uv;\n"
    "void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));\n"
    "  uv=p; gl_Position=vec4(p*2.0-1.0,0.0,1.0); }\n";
const char* kFS =
    "#version 300 es\n"
    "precision highp float;\n"
    "in vec2 uv; uniform sampler2D tex; out vec4 color;\n"
    "void main(){ float r=texture(tex,uv).r; color=vec4(r,0.0,0.0,1.0); }\n";

GLuint compile(GLenum type, const char* src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, nullptr);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512];
    glGetShaderInfoLog(s, 512, nullptr, log);
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "GLES shader compile: %s", log);
  }
  return s;
}

// Upload a full R8 image + several sub-rect updates at unaligned offsets, sample
// NEAREST 1:1 to an RGBA8 FBO, read back, compare. Returns mismatch count.
int gles_roundtrip(uint32_t W, uint32_t H, GLuint prog, bool subrects, int* fx, int* fy, int* fe,
                   int* fg) {
  std::vector<uint8_t> exp(static_cast<size_t>(W) * H);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
      exp[y * W + x] = static_cast<uint8_t>((x * 131u + y * 17u) & 0xFFu);

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, exp.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (subrects) {
    struct R {
      uint32_t ox, oy, w, h;
    };
    const R rects[] = {{0, 0, 16, 16},    {37, 91, 24, 11},  {130, 7, 40, 33},
                       {201, 150, 50, 60}, {7, 200, 13, 19}};
    for (int r = 0; r < 5; r++) {
      const R& rc = rects[r];
      if (rc.ox + rc.w > W || rc.oy + rc.h > H) continue;
      std::vector<uint8_t> sub(static_cast<size_t>(rc.w) * rc.h);
      for (uint32_t ly = 0; ly < rc.h; ly++)
        for (uint32_t lx = 0; lx < rc.w; lx++) {
          uint8_t v = static_cast<uint8_t>((lx * 53u + ly * 29u + (r + 1) * 97u) & 0xFFu);
          sub[ly * rc.w + lx] = v;
          exp[(rc.oy + ly) * W + (rc.ox + lx)] = v;
        }
      glTexSubImage2D(GL_TEXTURE_2D, 0, rc.ox, rc.oy, rc.w, rc.h, GL_RED, GL_UNSIGNED_BYTE,
                      sub.data());
    }
  }

  GLuint fbo, rt;
  glGenTextures(1, &rt);
  glBindTexture(GL_TEXTURE_2D, rt);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, W, H, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glGenFramebuffers(1, &fbo);
  glBindFramebuffer(GL_FRAMEBUFFER, fbo);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt, 0);
  glViewport(0, 0, W, H);

  glUseProgram(prog);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(glGetUniformLocation(prog, "tex"), 0);
  glDrawArrays(GL_TRIANGLES, 0, 3);

  std::vector<uint8_t> rb(static_cast<size_t>(W) * H * 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rb.data());

  int mm = 0;
  for (uint32_t i = 0; i < W * H; i++) {
    if (rb[i * 4] != exp[i]) {
      if (mm == 0) {
        *fx = static_cast<int>(i % W);
        *fy = static_cast<int>(i / W);
        *fe = exp[i];
        *fg = rb[i * 4];
      }
      mm++;
    }
  }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteTextures(1, &tex);
  glDeleteTextures(1, &rt);
  glDeleteFramebuffers(1, &fbo);
  return mm;
}

}  // namespace

// Called from the JNI probe in native-lib.cpp. Appends results to *out.
bool RunGlesProbe(std::string* out) {
  char buf[256];
  EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (dpy == EGL_NO_DISPLAY) {
    *out += "  EGL: no display\n";
    return false;
  }
  eglInitialize(dpy, nullptr, nullptr);
  EGLint cfgattr[] = {EGL_SURFACE_TYPE,    EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                      EGL_OPENGL_ES3_BIT,  EGL_RED_SIZE,    8,
                      EGL_GREEN_SIZE,      8,               EGL_BLUE_SIZE,
                      8,                   EGL_ALPHA_SIZE,  8,
                      EGL_NONE};
  EGLConfig cfg;
  EGLint nc = 0;
  eglChooseConfig(dpy, cfgattr, &cfg, 1, &nc);
  if (nc < 1) {
    *out += "  EGL: no config\n";
    return false;
  }
  EGLint pb[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pb);
  EGLint ctxattr[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxattr);
  if (ctx == EGL_NO_CONTEXT) {
    *out += "  EGL: no context\n";
    return false;
  }
  eglMakeCurrent(dpy, surf, surf, ctx);
  snprintf(buf, sizeof(buf), "  GL_RENDERER: %s\n",
           reinterpret_cast<const char*>(glGetString(GL_RENDERER)));
  *out += buf;

  GLuint vs = compile(GL_VERTEX_SHADER, kVS), fs = compile(GL_FRAGMENT_SHADER, kFS);
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs);
  glAttachShader(prog, fs);
  glLinkProgram(prog);

  struct S {
    uint32_t w, h;
  };
  const S sizes[] = {{64, 64}, {256, 256}, {300, 200}, {137, 59}, {1024, 64}};
  int fail = 0, total = 0;
  for (int s = 0; s < 5; s++) {
    int fx = -1, fy = -1, fe = -1, fg = -1;
    int mm = gles_roundtrip(sizes[s].w, sizes[s].h, prog, /*subrects=*/false, &fx, &fy, &fe, &fg);
    total++;
    if (mm == 0) {
      snprintf(buf, sizeof(buf), "  %4ux%-4u : OK\n", sizes[s].w, sizes[s].h);
    } else {
      fail++;
      snprintf(buf, sizeof(buf), "  %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n",
               sizes[s].w, sizes[s].h, mm, fx, fy, fe, fg);
    }
    *out += buf;
  }
  // Sub-rect atlas-update variant (Chromium per-glyph placement) at 256x256/512x512.
  const S atlas[] = {{256, 256}, {512, 512}};
  int gfail = 0;
  for (int s = 0; s < 2; s++) {
    int fx = -1, fy = -1, fe = -1, fg = -1;
    int mm = gles_roundtrip(atlas[s].w, atlas[s].h, prog, /*subrects=*/true, &fx, &fy, &fe, &fg);
    total++;
    if (mm == 0) {
      snprintf(buf, sizeof(buf), "  subrect %4ux%-4u : OK\n", atlas[s].w, atlas[s].h);
    } else {
      gfail++;
      fail++;
      snprintf(buf, sizeof(buf),
               "  subrect %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n",
               atlas[s].w, atlas[s].h, mm, fx, fy, fe, fg);
    }
    *out += buf;
  }
  snprintf(buf, sizeof(buf), "GLES: %d/%d clean%s\n", total - fail, total,
           fail ? "  <-- GLES R8 PATH CORRUPTED (Chromium's path!)" : "");
  *out += buf;

  glDeleteProgram(prog);
  glDeleteShader(vs);
  glDeleteShader(fs);
  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(dpy, ctx);
  eglDestroySurface(dpy, surf);
  eglTerminate(dpy);
  return fail == 0;
}
