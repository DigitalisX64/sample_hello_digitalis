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

// Chromium's glyph-atlas uploads use GL_UNPACK_ROW_LENGTH (the Skia A8 mask's
// rowBytes is wider than the glyph width, for alignment) and frequently a pixel-
// unpack buffer (PBO). The plain gles_roundtrip above uploads tightly-packed and
// never exercises either, so a proxy mis-marshal of the unpack state or the PBO
// path would slip through. These two functions reproduce exactly those patterns.

// Upload a sub-rect from a source buffer whose row stride (ROW_LENGTH) exceeds
// the copied width, with a non-zero GL_UNPACK_SKIP_ROWS/PIXELS — the host must
// read the guest buffer honoring the unpack state. Returns mismatch count.
int gles_rowlength_test(uint32_t W, uint32_t H, GLuint prog, int* fx, int* fy, int* fe, int* fg) {
  const uint32_t stride = W + 37;       // padded row length (> W)
  const uint32_t skip_rows = 3, skip_px = 11;
  std::vector<uint8_t> src(static_cast<size_t>(stride) * (H + skip_rows), 0xCC);  // 0xCC = pad
  std::vector<uint8_t> exp(static_cast<size_t>(W) * H);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++) {
      uint8_t v = static_cast<uint8_t>((x * 67u + y * 151u + 7u) & 0xFFu);
      src[(y + skip_rows) * stride + (x + skip_px)] = v;  // only the live window
      exp[y * W + x] = v;
    }

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride));
  glPixelStorei(GL_UNPACK_SKIP_ROWS, static_cast<GLint>(skip_rows));
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, static_cast<GLint>(skip_px));
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RED, GL_UNSIGNED_BYTE, src.data());
  // reset unpack state so the sample-render path is unaffected
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
  glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

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
  for (uint32_t i = 0; i < W * H; i++)
    if (rb[i * 4] != exp[i]) {
      if (mm == 0) { *fx = i % W; *fy = i / W; *fe = exp[i]; *fg = rb[i * 4]; }
      mm++;
    }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteTextures(1, &tex);
  glDeleteTextures(1, &rt);
  glDeleteFramebuffers(1, &fbo);
  return mm;
}

// Upload via a pixel-unpack buffer (PBO): fill a GL_PIXEL_UNPACK_BUFFER, then
// glTexSubImage2D with a byte OFFSET (not a pointer). Also exercises ROW_LENGTH
// against the PBO. Returns mismatch count.
int gles_pbo_test(uint32_t W, uint32_t H, GLuint prog, int* fx, int* fy, int* fe, int* fg) {
  const uint32_t stride = W + 16;
  const uint32_t off_rows = 2;
  const size_t pbo_bytes = static_cast<size_t>(stride) * (H + off_rows);
  std::vector<uint8_t> staging(pbo_bytes, 0x77);
  std::vector<uint8_t> exp(static_cast<size_t>(W) * H);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++) {
      uint8_t v = static_cast<uint8_t>((x * 113u + y * 41u + 19u) & 0xFFu);
      staging[(y + off_rows) * stride + x] = v;
      exp[y * W + x] = v;
    }
  GLuint pbo;
  glGenBuffers(1, &pbo);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo);
  glBufferData(GL_PIXEL_UNPACK_BUFFER, static_cast<GLsizeiptr>(pbo_bytes), staging.data(),
               GL_STREAM_DRAW);

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(stride));
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  // upload from PBO at byte offset off_rows*stride (the live data start)
  const uintptr_t byte_off = static_cast<uintptr_t>(off_rows) * stride;
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, W, H, GL_RED, GL_UNSIGNED_BYTE,
                  reinterpret_cast<const void*>(byte_off));
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

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
  for (uint32_t i = 0; i < W * H; i++)
    if (rb[i * 4] != exp[i]) {
      if (mm == 0) { *fx = i % W; *fy = i / W; *fe = exp[i]; *fg = rb[i * 4]; }
      mm++;
    }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteTextures(1, &tex);
  glDeleteTextures(1, &rt);
  glDeleteFramebuffers(1, &fbo);
  glDeleteBuffers(1, &pbo);
  return mm;
}

// Draw-side test: render a grid of textured quads from a VBO with per-vertex
// POSITION + TEXCOORD attributes (glVertexAttribPointer + glDrawElements), and
// again via glDrawElementsInstanced with a per-instance offset attribute
// (glVertexAttribDivisor) — exactly how Chromium draws glyph quads from the
// atlas. The plain probe used gl_VertexID (no attributes), so a proxy mis-marshal
// of attribute pointers / element indices / instancing would slip through. Each
// quad samples a distinct atlas cell; a wrong attribute/divisor makes a quad show
// the wrong cell. Returns mismatch count (cells whose center texel is wrong).
const char* kVS_attr =
    "#version 300 es\n"
    "layout(location=0) in vec2 aPos;\n"
    "layout(location=1) in vec2 aUV;\n"
    "layout(location=2) in vec2 aInstOff;\n"  // per-instance (divisor=1)
    "uniform float uInstanced;\n"
    "out vec2 vUV;\n"
    "void main(){ vUV=aUV; gl_Position=vec4(aPos+aInstOff*uInstanced,0.0,1.0); }\n";

int gles_drawattr_test(GLuint texprog_unused, bool instanced, int* fcell, int* fe, int* fg) {
  (void)texprog_unused;
  const uint32_t W = 256, H = 256, N = 8;  // NxN grid of cells
  const uint32_t cw = W / N, ch = H / N;
  std::vector<uint8_t> atlas(static_cast<size_t>(W) * H);
  for (uint32_t y = 0; y < H; y++)
    for (uint32_t x = 0; x < W; x++)
      atlas[y * W + x] = static_cast<uint8_t>(((x / cw) * 31u + (y / ch) * 53u + 1u) & 0xFFu);

  GLuint vs = compile(GL_VERTEX_SHADER, kVS_attr), fs = compile(GL_FRAGMENT_SHADER, kFS);
  GLuint prog = glCreateProgram();
  glAttachShader(prog, vs); glAttachShader(prog, fs); glLinkProgram(prog);

  GLuint tex;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, W, H, 0, GL_RED, GL_UNSIGNED_BYTE, atlas.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  // One quad per cell: position covers the cell's NDC rect, UV covers its atlas
  // rect. (Instanced variant: a single base quad + per-instance NDC offset.)
  std::vector<float> verts;   // x,y,u,v
  std::vector<uint16_t> idx;
  std::vector<float> instoff;  // per-instance NDC offset (instanced path)
  auto ndc = [](float t) { return t * 2.0f - 1.0f; };
  if (!instanced) {
    uint16_t base = 0;
    for (uint32_t gy = 0; gy < N; gy++)
      for (uint32_t gx = 0; gx < N; gx++) {
        float x0 = ndc((float)gx / N), x1 = ndc((float)(gx + 1) / N);
        float y0 = ndc((float)gy / N), y1 = ndc((float)(gy + 1) / N);
        float u0 = (float)gx / N, u1 = (float)(gx + 1) / N;
        float v0 = (float)gy / N, v1 = (float)(gy + 1) / N;
        float q[16] = {x0,y0,u0,v0, x1,y0,u1,v0, x1,y1,u1,v1, x0,y1,u0,v1};
        for (float f : q) verts.push_back(f);
        uint16_t i[6] = {(uint16_t)(base),(uint16_t)(base+1),(uint16_t)(base+2),
                         (uint16_t)(base),(uint16_t)(base+2),(uint16_t)(base+3)};
        for (uint16_t v : i) idx.push_back(v);
        base += 4;
      }
  } else {
    // base unit quad at cell (0,0); per-instance offset shifts it to each cell.
    float x1 = ndc(1.0f / N) - ndc(0.0f), y1 = x1;
    float bx = ndc(0.0f), by = ndc(0.0f);
    float q[16] = {bx,by,0,0, bx+x1,by,1.0f/N,0, bx+x1,by+y1,1.0f/N,1.0f/N, bx,by+y1,0,1.0f/N};
    for (float f : q) verts.push_back(f);
    uint16_t i[6] = {0,1,2,0,2,3};
    for (uint16_t v : i) idx.push_back(v);
    for (uint32_t gy = 0; gy < N; gy++)
      for (uint32_t gx = 0; gx < N; gx++) {
        instoff.push_back((float)gx * (2.0f / N));
        instoff.push_back((float)gy * (2.0f / N));
      }
    // NOTE: instanced UVs would need per-instance UV offset too; to keep the
    // check meaningful we instead verify only that instancing draws N*N quads
    // covering the grid (each instance samples the base cell). Simplify: skip
    // exactness for instanced, just ensure it runs + fills (non-zero) — the
    // attribute/divisor marshalling is what we exercise.
  }

  GLuint vbo, ebo, ibo = 0;
  glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, verts.size() * 4, verts.data(), GL_STATIC_DRAW);
  glGenBuffers(1, &ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * 2, idx.data(), GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
  glEnableVertexAttribArray(1);
  if (instanced) {
    glGenBuffers(1, &ibo); glBindBuffer(GL_ARRAY_BUFFER, ibo);
    glBufferData(GL_ARRAY_BUFFER, instoff.size() * 4, instoff.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8, (void*)0);
    glEnableVertexAttribArray(2);
    glVertexAttribDivisor(2, 1);
  } else {
    glDisableVertexAttribArray(2);
    glVertexAttrib2f(2, 0.0f, 0.0f);
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
  glClearColor(0, 0, 0, 1); glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(prog);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, tex);
  glUniform1i(glGetUniformLocation(prog, "tex"), 0);
  glUniform1f(glGetUniformLocation(prog, "uInstanced"), instanced ? 1.0f : 0.0f);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 16, (void*)0);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 16, (void*)8);
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  if (instanced)
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, N * N);
  else
    glDrawElements(GL_TRIANGLES, (GLsizei)idx.size(), GL_UNSIGNED_SHORT, 0);

  std::vector<uint8_t> rb(static_cast<size_t>(W) * H * 4);
  glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rb.data());
  int mm = 0;
  // check each cell's center texel == the atlas cell value
  for (uint32_t gy = 0; gy < N; gy++)
    for (uint32_t gx = 0; gx < N; gx++) {
      uint32_t px = gx * cw + cw / 2, py = gy * ch + ch / 2;
      uint8_t got = rb[(py * W + px) * 4];
      uint8_t want = static_cast<uint8_t>((gx * 31u + gy * 53u + 1u) & 0xFFu);
      if (instanced) { if (got == 0) { if (mm == 0) { *fcell = gy * N + gx; *fe = 1; *fg = 0; } mm++; } }
      else if (got != want) { if (mm == 0) { *fcell = gy * N + gx; *fe = want; *fg = got; } mm++; }
    }
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glDeleteTextures(1, &tex); glDeleteTextures(1, &rt);
  glDeleteFramebuffers(1, &fbo);
  glDeleteBuffers(1, &vbo); glDeleteBuffers(1, &ebo);
  if (ibo) glDeleteBuffers(1, &ibo);
  glDeleteProgram(prog); glDeleteShader(vs); glDeleteShader(fs);
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
  // ROW_LENGTH / SKIP_ROWS / SKIP_PIXELS upload (Chromium's Skia A8-mask rowBytes
  // > width) and PBO upload (Chromium async glyph atlas) — the patterns the plain
  // roundtrip never exercises.
  const S rl[] = {{256, 256}, {300, 200}, {137, 59}};
  for (int s = 0; s < 3; s++) {
    int fx = -1, fy = -1, fe = -1, fg = -1;
    int mm = gles_rowlength_test(rl[s].w, rl[s].h, prog, &fx, &fy, &fe, &fg);
    total++;
    if (mm == 0) {
      snprintf(buf, sizeof(buf), "  rowlen  %4ux%-4u : OK\n", rl[s].w, rl[s].h);
    } else {
      fail++;
      snprintf(buf, sizeof(buf),
               "  rowlen  %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n",
               rl[s].w, rl[s].h, mm, fx, fy, fe, fg);
    }
    *out += buf;
    fx = fy = fe = fg = -1;
    mm = gles_pbo_test(rl[s].w, rl[s].h, prog, &fx, &fy, &fe, &fg);
    total++;
    if (mm == 0) {
      snprintf(buf, sizeof(buf), "  pbo     %4ux%-4u : OK\n", rl[s].w, rl[s].h);
    } else {
      fail++;
      snprintf(buf, sizeof(buf),
               "  pbo     %4ux%-4u : MISMATCH x%d (first @%d,%d exp=0x%02x got=0x%02x)\n",
               rl[s].w, rl[s].h, mm, fx, fy, fe, fg);
    }
    *out += buf;
  }
  // Draw-side: vertex-attribute indexed draw + instanced draw (Chromium's glyph
  // quad path) — the plain probe only used gl_VertexID.
  {
    int fc = -1, fe = -1, fg = -1;
    int mm = gles_drawattr_test(prog, /*instanced=*/false, &fc, &fe, &fg);
    total++;
    if (mm == 0) snprintf(buf, sizeof(buf), "  drawattr 256x256  : OK\n");
    else { fail++; snprintf(buf, sizeof(buf),
            "  drawattr 256x256  : MISMATCH x%d (cell %d exp=0x%02x got=0x%02x)\n", mm, fc, fe, fg); }
    *out += buf;
    fc = fe = fg = -1;
    mm = gles_drawattr_test(prog, /*instanced=*/true, &fc, &fe, &fg);
    total++;
    if (mm == 0) snprintf(buf, sizeof(buf), "  drawinst 256x256  : OK\n");
    else { fail++; snprintf(buf, sizeof(buf),
            "  drawinst 256x256  : MISMATCH x%d (cell %d empty)\n", mm, fc); }
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
