// Integration-level probe for eglGetProcAddress extension-proc coverage under
// translation — the proxy-layer contract that Chromium-class apps depend on.
//
// The invariant under test: for every GL/EGL extension the host driver
// ADVERTISES (GL_EXTENSIONS / eglQueryString), eglGetProcAddress must return a
// non-NULL, guest-callable entry point for that extension's functions. Real
// engines (Chromium's GL bindings among them) gate calls on the extension
// string, not on the probed pointer — an advertised extension whose proc comes
// back NULL means the app calls NULL and the process dies at guest PC 0. This
// exact gap (ANGLE advertising GL_ANGLE_robust_client_memory while the proxy
// NULLed glGetIntegervRobustANGLE) crashed the Chromium GPU process in a loop.
//
// The probe creates a real ES2 pbuffer context, then:
//   1. For each covered extension advertised by the driver, requires
//      eglGetProcAddress to return non-NULL for its entry points (the
//      advertised-but-NULL landmine aborts).
//   2. CALLS the wrapped procs and cross-checks them against the core API:
//      glGetIntegervRobustANGLE / glGetFloatvRobustANGLE / glGetBooleanvRobustANGLE
//      and glGetShaderivRobustANGLE must agree with glGetIntegerv/gl*, and
//      glGetTexLevelParameterivANGLE must return a created texture's real size.
//   3. Registers a guest callback through eglDebugMessageControlKHR (EGL_KHR_debug)
//      — exercising the host->guest callback wrapping — and requires EGL_SUCCESS.
// Any violation aborts() so the sample suite flags a crash; a clean pass logs a
// report. Extensions the driver does not advertise are skipped (NULL is the
// correct answer for those).

#include <android/log.h>
#include <jni.h>

#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#define LOG_TAG "helloeglext"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define AFAIL(...)                                                   \
  do {                                                               \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__);    \
    abort();                                                         \
  } while (0)

namespace {

// ANGLE extension prototypes (not in the NDK GLES2 headers).
using PFNGLGETINTEGERVROBUSTANGLE = void (*)(GLenum, GLsizei, GLsizei*, GLint*);
using PFNGLGETFLOATVROBUSTANGLE = void (*)(GLenum, GLsizei, GLsizei*, GLfloat*);
using PFNGLGETBOOLEANVROBUSTANGLE = void (*)(GLenum, GLsizei, GLsizei*, GLboolean*);
using PFNGLGETSHADERIVROBUSTANGLE = void (*)(GLuint, GLenum, GLsizei, GLsizei*, GLint*);
using PFNGLGETTEXLEVELPARAMETERIVANGLE = void (*)(GLenum, GLint, GLenum, GLint*);
using PFNGLPOLYGONMODEANGLE = void (*)(GLenum, GLenum);
// EGL_KHR_debug.
using EGLDEBUGPROCKHR = void (*)(GLenum error, const char* command, EGLint messageType,
                                 void* threadLabel, void* objectLabel, const char* message);
using PFNEGLDEBUGMESSAGECONTROLKHR = EGLint (*)(EGLDEBUGPROCKHR, const intptr_t*);

constexpr GLenum kGlTexture2D = 0x0DE1;
constexpr GLenum kGlTextureWidth = 0x1000;   // GL_TEXTURE_WIDTH
constexpr GLenum kGlTextureHeight = 0x1001;  // GL_TEXTURE_HEIGHT

bool HasToken(const char* haystack, const char* token) {
  // Extension strings are space-separated full tokens.
  if (haystack == nullptr) return false;
  size_t len = strlen(token);
  for (const char* p = haystack; (p = strstr(p, token)) != nullptr; p += len) {
    bool starts = (p == haystack || p[-1] == ' ');
    bool ends = (p[len] == '\0' || p[len] == ' ');
    if (starts && ends) return true;
  }
  return false;
}

int g_debug_callback_hits = 0;
void DebugCallback(GLenum /*error*/, const char* /*command*/, EGLint /*messageType*/,
                   void* /*threadLabel*/, void* /*objectLabel*/, const char* /*message*/) {
  g_debug_callback_hits++;
}

struct EglContext {
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLSurface surface = EGL_NO_SURFACE;
  EGLContext context = EGL_NO_CONTEXT;
};

EglContext CreatePbufferContext() {
  EglContext ec;
  ec.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (ec.display == EGL_NO_DISPLAY || eglInitialize(ec.display, nullptr, nullptr) != EGL_TRUE) {
    AFAIL("eglInitialize did not succeed");
  }
  const EGLint config_attribs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                                   EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                                   EGL_NONE};
  EGLConfig config;
  EGLint num_configs = 0;
  if (eglChooseConfig(ec.display, config_attribs, &config, 1, &num_configs) != EGL_TRUE ||
      num_configs < 1) {
    AFAIL("eglChooseConfig found no pbuffer ES2 config");
  }
  const EGLint pbuffer_attribs[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
  ec.surface = eglCreatePbufferSurface(ec.display, config, pbuffer_attribs);
  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  ec.context = eglCreateContext(ec.display, config, EGL_NO_CONTEXT, context_attribs);
  if (ec.surface == EGL_NO_SURFACE || ec.context == EGL_NO_CONTEXT ||
      eglMakeCurrent(ec.display, ec.surface, ec.surface, ec.context) != EGL_TRUE) {
    AFAIL("eglMakeCurrent did not succeed");
  }
  return ec;
}

// Probes `name` and enforces the advertised-implies-non-NULL contract.
void* MustGetProc(bool advertised, const char* name, std::string* report) {
  void* fn = reinterpret_cast<void*>(eglGetProcAddress(name));
  if (advertised && fn == nullptr) {
    AFAIL("advertised extension proc \"%s\" resolved NULL — the landmine callers "
          "jump to; aborting", name);
  }
  char line[160];
  snprintf(line, sizeof(line), "  %-42s %s\n", name,
           fn != nullptr ? "wrapped" : "absent (extension not advertised)");
  *report += line;
  return fn;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_helloeglext_MainActivity_probeEglext(JNIEnv* env, jobject /*this*/) {
  std::string report = "eglGetProcAddress extension-proc probe:\n";

  EglContext ec = CreatePbufferContext();

  const char* gl_exts = reinterpret_cast<const char*>(glGetString(GL_EXTENSIONS));
  const char* egl_exts = eglQueryString(ec.display, EGL_EXTENSIONS);
  const bool has_robust = HasToken(gl_exts, "GL_ANGLE_robust_client_memory");
  const bool has_texlevel = HasToken(gl_exts, "GL_ANGLE_get_tex_level_parameter");
  const bool has_polygon = HasToken(gl_exts, "GL_ANGLE_polygon_mode");
  const bool has_egl_debug = HasToken(egl_exts, "EGL_KHR_debug");

  char hdr[160];
  snprintf(hdr, sizeof(hdr),
           "  driver advertises: robust=%d tex_level=%d polygon_mode=%d egl_debug=%d\n",
           has_robust, has_texlevel, has_polygon, has_egl_debug);
  report += hdr;

  // 1) Advertised-implies-non-NULL across the newly covered families.
  auto* robust_geti = reinterpret_cast<PFNGLGETINTEGERVROBUSTANGLE>(
      MustGetProc(has_robust, "glGetIntegervRobustANGLE", &report));
  auto* robust_getf = reinterpret_cast<PFNGLGETFLOATVROBUSTANGLE>(
      MustGetProc(has_robust, "glGetFloatvRobustANGLE", &report));
  auto* robust_getb = reinterpret_cast<PFNGLGETBOOLEANVROBUSTANGLE>(
      MustGetProc(has_robust, "glGetBooleanvRobustANGLE", &report));
  auto* robust_shader = reinterpret_cast<PFNGLGETSHADERIVROBUSTANGLE>(
      MustGetProc(has_robust, "glGetShaderivRobustANGLE", &report));
  auto* texlevel_geti = reinterpret_cast<PFNGLGETTEXLEVELPARAMETERIVANGLE>(
      MustGetProc(has_texlevel, "glGetTexLevelParameterivANGLE", &report));
  auto* polygon_mode = reinterpret_cast<PFNGLPOLYGONMODEANGLE>(
      MustGetProc(has_polygon, "glPolygonModeANGLE", &report));
  auto* egl_debug_control = reinterpret_cast<PFNEGLDEBUGMESSAGECONTROLKHR>(
      MustGetProc(has_egl_debug, "eglDebugMessageControlKHR", &report));

  // 2) Call the wrapped getters and cross-check against the core API. Repeat a
  //    few times so a marshalling bug that corrupts state trips deterministically.
  if (robust_geti != nullptr && robust_getf != nullptr && robust_getb != nullptr) {
    for (int i = 0; i < 100; i++) {
      GLint core_max_tex = 0, robust_max_tex = 0;
      GLsizei out_len = 0;
      glGetIntegerv(GL_MAX_TEXTURE_SIZE, &core_max_tex);
      robust_geti(GL_MAX_TEXTURE_SIZE, 1, &out_len, &robust_max_tex);
      if (robust_max_tex != core_max_tex || out_len != 1) {
        AFAIL("glGetIntegervRobustANGLE mismatch: robust=%d len=%d core=%d",
              robust_max_tex, out_len, core_max_tex);
      }

      GLfloat core_range[2] = {-1, -1}, robust_range[2] = {-2, -2};
      glGetFloatv(GL_ALIASED_LINE_WIDTH_RANGE, core_range);
      robust_getf(GL_ALIASED_LINE_WIDTH_RANGE, 2, &out_len, robust_range);
      if (robust_range[0] != core_range[0] || robust_range[1] != core_range[1]) {
        AFAIL("glGetFloatvRobustANGLE mismatch: [%f,%f] vs core [%f,%f]",
              robust_range[0], robust_range[1], core_range[0], core_range[1]);
      }

      GLboolean core_dither = 0xAA, robust_dither = 0x55;
      glGetBooleanv(GL_DITHER, &core_dither);
      robust_getb(GL_DITHER, 1, &out_len, &robust_dither);
      if (robust_dither != core_dither) {
        AFAIL("glGetBooleanvRobustANGLE mismatch: %d vs core %d",
              robust_dither, core_dither);
      }
    }
    report += "  robust getters agree with core API x100: OK\n";
  }

  if (robust_shader != nullptr) {
    GLuint shader = glCreateShader(GL_VERTEX_SHADER);
    const char* src = "void main() { gl_Position = vec4(0.0); }";
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint core_status = -1, robust_status = -2;
    GLsizei out_len = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &core_status);
    robust_shader(shader, GL_COMPILE_STATUS, 1, &out_len, &robust_status);
    if (robust_status != core_status || core_status != GL_TRUE) {
      AFAIL("glGetShaderivRobustANGLE mismatch: robust=%d core=%d",
            robust_status, core_status);
    }
    glDeleteShader(shader);
    report += "  glGetShaderivRobustANGLE compile-status: OK\n";
  }

  if (texlevel_geti != nullptr) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(kGlTexture2D, tex);
    glTexImage2D(kGlTexture2D, 0, GL_RGBA, 8, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    GLint w = -1, h = -1;
    texlevel_geti(kGlTexture2D, 0, kGlTextureWidth, &w);
    texlevel_geti(kGlTexture2D, 0, kGlTextureHeight, &h);
    if (w != 8 || h != 4) {
      AFAIL("glGetTexLevelParameterivANGLE returned %dx%d for an 8x4 texture", w, h);
    }
    glDeleteTextures(1, &tex);
    report += "  glGetTexLevelParameterivANGLE 8x4 texture: OK\n";
  }

  if (polygon_mode != nullptr) {
    while (glGetError() != GL_NO_ERROR) {
    }
    polygon_mode(GL_FRONT_AND_BACK, 0x1B02 /* GL_FILL_ANGLE */);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
      AFAIL("glPolygonModeANGLE(GL_FILL) raised GL error 0x%x", err);
    }
    report += "  glPolygonModeANGLE(GL_FILL): OK\n";
  }

  // 3) Guest-callback wrapping: register + unregister a debug callback.
  if (egl_debug_control != nullptr) {
    EGLint rc = egl_debug_control(&DebugCallback, nullptr);
    if (rc != EGL_SUCCESS) {
      AFAIL("eglDebugMessageControlKHR(register) returned 0x%x", rc);
    }
    rc = egl_debug_control(nullptr, nullptr);
    if (rc != EGL_SUCCESS) {
      AFAIL("eglDebugMessageControlKHR(unregister) returned 0x%x", rc);
    }
    char line[96];
    snprintf(line, sizeof(line),
             "  eglDebugMessageControlKHR register/unregister: OK (hits=%d)\n",
             g_debug_callback_hits);
    report += line;
  }

  eglMakeCurrent(ec.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(ec.display, ec.context);
  eglDestroySurface(ec.display, ec.surface);

  ALOG("%s", report.c_str());
  return env->NewStringUTF(report.c_str());
}
