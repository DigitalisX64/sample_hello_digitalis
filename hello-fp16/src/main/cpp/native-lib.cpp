// hello-fp16: integration-level probe for Armv8.2-FP16 scalar arithmetic
// (Plan §E1 / §M1).
//
// The Berberis arm64 decoder accepts ftype=11 (half-precision) in
// FpDataProc1/FpDataProc2/FpDataProc3/FpCompare/FpCondSelect/
// FpMovImmediate and the interpreter rounds through float for the
// arithmetic + the standard FpHalfToSingle / FpSingleToHalf helpers.
//
// Probes (all use Hd, Hn, Hm scalar form, ftype=11):
//
//   FpDataProc2 (2-source, opcode at bits[15:12]):
//     0010 FADD     0011 FSUB
//     0000 FMUL    0001 FDIV
//     0100 FMAX    0101 FMIN
//     0110 FMAXNM  0111 FMINNM
//     1000 FNMUL
//
//   FpDataProc3 (3-source, o1@21 o0@15):
//     o1=0 o0=0 FMADD     o1=0 o0=1 FMSUB
//     o1=1 o0=0 FNMADD    o1=1 o0=1 FNMSUB
//
//   FpDataProc1 (1-source, opcode at bits[20:15]):
//     000000 FMOV   000001 FABS   000010 FNEG   000011 FSQRT
//     001000 FRINTN 001001 FRINTP 001010 FRINTM 001011 FRINTZ
//     001100 FRINTA 001110 FRINTX 001111 FRINTI
//
//   FpCompare:
//     FCMP Hn, Hm      FCMP Hn, #0.0      FCMPE Hn, Hm
//
//   FpCondSelect:    FCSEL Hd, Hn, Hm, cond
//   FpMovImmediate:  FMOV Hd, #<imm>
//
// llvm-mc-verified encodings (clang --target=aarch64 -march=armv8.2-a+fp16):
//   0x1ee22820  fadd  h0, h1, h2
//   0x1ee23820  fsub  h0, h1, h2
//   0x1ee20820  fmul  h0, h1, h2
//   0x1ee21820  fdiv  h0, h1, h2
//   0x1ee24820  fmax  h0, h1, h2
//   0x1ee25820  fmin  h0, h1, h2
//   0x1ee26820  fmaxnm h0, h1, h2
//   0x1ee27820  fminnm h0, h1, h2
//   0x1ee28820  fnmul h0, h1, h2
//   0x1fc20c20  fmadd h0, h1, h2, h3
//   0x1fc28c20  fmsub h0, h1, h2, h3
//   0x1fe20c20  fnmadd h0, h1, h2, h3
//   0x1fe28c20  fnmsub h0, h1, h2, h3
//   0x1ee0c020  fabs   h0, h1
//   0x1ee14020  fneg   h0, h1
//   0x1ee1c020  fsqrt  h0, h1
//   0x1ee04020  fmov   h0, h1
//   0x1ee44020  frintn h0, h1
//   0x1ee22020  fcmp   h1, h2
//   0x1ee02028  fcmp   h1, #0.0
//   0x1ee22030  fcmpe  h1, h2
//   0x1ee20c20  fcsel  h0, h1, h2, eq
//   0x1eee1000  fmov   h0, #1.0
//
// Inputs cover finite values that round non-trivially back to FP16
// (e.g. 1.0/3.0, sqrt(2), small denormals) and edge cases (0.0,
// negative zero, ±inf via overflowed exponent).  A SF<->DF
// mis-dispatch would silently corrupt the result, so the reference is
// computed via the host's float arithmetic followed by an IEEE-754
// binary32->binary16 conversion.

#include <android/log.h>
#include <jni.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "hellofp16"

namespace {

// IEEE 754 binary32 -> binary16 ("F16C round-to-nearest-even" semantics).
// Used to compute the reference for each probe: we do the math in float
// and then narrow back to the FP16 bit pattern.
uint16_t SingleToHalf(float f) {
  uint32_t fbits;
  std::memcpy(&fbits, &f, 4);
  uint16_t sign = (fbits >> 16) & 0x8000;
  int32_t exp = ((fbits >> 23) & 0xFF) - 127;
  uint32_t frac = fbits & 0x7FFFFF;

  if (exp == 128) {
    return sign | 0x7C00 | (frac ? (frac >> 13) | 1 : 0);
  }
  if (exp > 15) return sign | 0x7C00;
  if (exp < -24) return sign;
  if (exp < -14) {
    frac = (frac | 0x800000) >> (-exp - 14 + 13);
    return sign | static_cast<uint16_t>(frac);
  }
  return sign | static_cast<uint16_t>((exp + 15) << 10) |
         static_cast<uint16_t>(frac >> 13);
}

// IEEE 754 binary16 -> binary32.
float HalfToSingle(uint16_t h) {
  uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
  uint32_t exp = (h >> 10) & 0x1F;
  uint32_t frac = h & 0x3FF;
  uint32_t fbits;
  if (exp == 0) {
    if (frac == 0) {
      fbits = sign;
    } else {
      exp = 1;
      while (!(frac & 0x400)) {
        frac <<= 1;
        exp--;
      }
      frac &= 0x3FF;
      fbits = sign | (static_cast<uint32_t>(exp + 127 - 15) << 23) | (frac << 13);
    }
  } else if (exp == 0x1F) {
    fbits = sign | 0x7F800000 | (frac << 13);
  } else {
    fbits = sign | (static_cast<uint32_t>(exp + 127 - 15) << 23) | (frac << 13);
  }
  float result;
  std::memcpy(&result, &fbits, 4);
  return result;
}

// Construct an FP16 register value at H0 by writing 16 bits into a
// uint16_t and loading into the SIMD register via LDR h0, [addr].
// Returns the result bits read back out of H0.

#define FP16_OP_2SRC(MNEMONIC)                                                 \
  uint16_t fp16_##MNEMONIC(uint16_t a, uint16_t b) {                           \
    uint16_t r;                                                                \
    asm volatile(                                                              \
        "ldr h1, [%[pa]]\n\t"                                                  \
        "ldr h2, [%[pb]]\n\t"                                                  \
        #MNEMONIC " h0, h1, h2\n\t"                                            \
        "str h0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(&a), [pb] "r"(&b), [pr] "r"(&r)                             \
        : "v0", "v1", "v2", "memory");                                         \
    return r;                                                                  \
  }

FP16_OP_2SRC(fadd)
FP16_OP_2SRC(fsub)
FP16_OP_2SRC(fmul)
FP16_OP_2SRC(fdiv)
FP16_OP_2SRC(fmax)
FP16_OP_2SRC(fmin)
FP16_OP_2SRC(fmaxnm)
FP16_OP_2SRC(fminnm)
FP16_OP_2SRC(fnmul)

// Scalar FP32 / FP64 two-source probes for the FpDataProc2 ftype=00 (S) and
// ftype=01 (D) JIT paths.  Plan §D1 / §M1: exercise the same five
// opcodes — FMAX / FMIN / FMAXNM / FMINNM / FNMUL — that the FP16 macro
// above covers, but at single- and double-precision so the JIT lane-0
// emit sequences are sample-tested.  llvm-mc-verified encodings:
//   0x1e224820 fmax  s0, s1, s2     0x1e624820 fmax  d0, d1, d2
//   0x1e225820 fmin  s0, s1, s2     0x1e625820 fmin  d0, d1, d2
//   0x1e226820 fmaxnm s0, s1, s2    0x1e626820 fmaxnm d0, d1, d2
//   0x1e227820 fminnm s0, s1, s2    0x1e627820 fminnm d0, d1, d2
//   0x1e228820 fnmul s0, s1, s2     0x1e628820 fnmul d0, d1, d2
#define FP32_OP_2SRC(MNEMONIC)                                                 \
  float fp32_##MNEMONIC(float a, float b) {                                    \
    float r;                                                                   \
    asm volatile(                                                              \
        "ldr s1, [%[pa]]\n\t"                                                  \
        "ldr s2, [%[pb]]\n\t"                                                  \
        #MNEMONIC " s0, s1, s2\n\t"                                            \
        "str s0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(&a), [pb] "r"(&b), [pr] "r"(&r)                             \
        : "v0", "v1", "v2", "memory");                                         \
    return r;                                                                  \
  }

FP32_OP_2SRC(fmax)
FP32_OP_2SRC(fmin)
FP32_OP_2SRC(fmaxnm)
FP32_OP_2SRC(fminnm)
FP32_OP_2SRC(fnmul)

#define FP64_OP_2SRC(MNEMONIC)                                                 \
  double fp64_##MNEMONIC(double a, double b) {                                 \
    double r;                                                                  \
    asm volatile(                                                              \
        "ldr d1, [%[pa]]\n\t"                                                  \
        "ldr d2, [%[pb]]\n\t"                                                  \
        #MNEMONIC " d0, d1, d2\n\t"                                            \
        "str d0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(&a), [pb] "r"(&b), [pr] "r"(&r)                             \
        : "v0", "v1", "v2", "memory");                                         \
    return r;                                                                  \
  }

FP64_OP_2SRC(fmax)
FP64_OP_2SRC(fmin)
FP64_OP_2SRC(fmaxnm)
FP64_OP_2SRC(fminnm)
FP64_OP_2SRC(fnmul)

#define FP16_OP_3SRC(MNEMONIC)                                                 \
  uint16_t fp16_##MNEMONIC(uint16_t a, uint16_t b, uint16_t c) {               \
    uint16_t r;                                                                \
    asm volatile(                                                              \
        "ldr h1, [%[pa]]\n\t"                                                  \
        "ldr h2, [%[pb]]\n\t"                                                  \
        "ldr h3, [%[pc]]\n\t"                                                  \
        #MNEMONIC " h0, h1, h2, h3\n\t"                                        \
        "str h0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(&a), [pb] "r"(&b), [pc] "r"(&c), [pr] "r"(&r)               \
        : "v0", "v1", "v2", "v3", "memory");                                   \
    return r;                                                                  \
  }

FP16_OP_3SRC(fmadd)
FP16_OP_3SRC(fmsub)
FP16_OP_3SRC(fnmadd)
FP16_OP_3SRC(fnmsub)

// FP32 / FP64 three-source FMADD / FMSUB / FNMADD / FNMSUB probes.
// These hit the FpDataProc3 JIT case arms (Vfmadd231ss / Vfnmadd231ss /
// Vfnmsub231ss / Vfmsub231ss for FP32, ...sd for FP64).  llvm-mc-verified
// encodings:
//   0x1f020c20 fmadd  s0, s1, s2, s3      0x1f420c20 fmadd  d0, d1, d2, d3
//   0x1f028c20 fmsub  s0, s1, s2, s3      0x1f428c20 fmsub  d0, d1, d2, d3
//   0x1f220c20 fnmadd s0, s1, s2, s3      0x1f620c20 fnmadd d0, d1, d2, d3
//   0x1f228c20 fnmsub s0, s1, s2, s3      0x1f628c20 fnmsub d0, d1, d2, d3
#define FP32_OP_3SRC(MNEMONIC)                                                 \
  float fp32_##MNEMONIC(float a, float b, float c) {                           \
    float r;                                                                   \
    asm volatile(                                                              \
        "ldr s1, [%[pa]]\n\t"                                                  \
        "ldr s2, [%[pb]]\n\t"                                                  \
        "ldr s3, [%[pc]]\n\t"                                                  \
        #MNEMONIC " s0, s1, s2, s3\n\t"                                        \
        "str s0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(&a), [pb] "r"(&b), [pc] "r"(&c), [pr] "r"(&r)               \
        : "v0", "v1", "v2", "v3", "memory");                                   \
    return r;                                                                  \
  }

FP32_OP_3SRC(fmadd)
FP32_OP_3SRC(fmsub)
FP32_OP_3SRC(fnmadd)
FP32_OP_3SRC(fnmsub)

#define FP64_OP_3SRC(MNEMONIC)                                                 \
  double fp64_##MNEMONIC(double a, double b, double c) {                       \
    double r;                                                                  \
    asm volatile(                                                              \
        "ldr d1, [%[pa]]\n\t"                                                  \
        "ldr d2, [%[pb]]\n\t"                                                  \
        "ldr d3, [%[pc]]\n\t"                                                  \
        #MNEMONIC " d0, d1, d2, d3\n\t"                                        \
        "str d0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(&a), [pb] "r"(&b), [pc] "r"(&c), [pr] "r"(&r)               \
        : "v0", "v1", "v2", "v3", "memory");                                   \
    return r;                                                                  \
  }

FP64_OP_3SRC(fmadd)
FP64_OP_3SRC(fmsub)
FP64_OP_3SRC(fnmadd)
FP64_OP_3SRC(fnmsub)

// SCVTF / UCVTF (scalar, integer→FP, rmode=00 in FpIntConversion).  Each
// helper takes one integer source register (W or X), executes the ARM
// convert, and stores the FP result back as raw bits — matching the
// existing fp_fcvt_*_* probes and letting the host reference be a plain
// (float)/(double) cast.
//
// llvm-mc-verified encodings (clang --target=aarch64 -march=armv8-a):
//   0x1e220020 scvtf s0, w1            0x1e620020 scvtf d0, w1
//   0x9e220020 scvtf s0, x1            0x9e620020 scvtf d0, x1
//   0x1e230020 ucvtf s0, w1            0x1e630020 ucvtf d0, w1
//   0x9e230020 ucvtf s0, x1            0x9e630020 ucvtf d0, x1
uint32_t fp_scvtf_s_w(int32_t a) {
  uint32_t r;
  asm volatile(
      "scvtf s0, %w[a]\n\t"
      "str s0, [%[pr]]\n\t"
      :
      : [a] "r"(a), [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

uint64_t fp_scvtf_d_w(int32_t a) {
  uint64_t r;
  asm volatile(
      "scvtf d0, %w[a]\n\t"
      "str d0, [%[pr]]\n\t"
      :
      : [a] "r"(a), [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

uint32_t fp_scvtf_s_x(int64_t a) {
  uint32_t r;
  asm volatile(
      "scvtf s0, %x[a]\n\t"
      "str s0, [%[pr]]\n\t"
      :
      : [a] "r"(a), [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

uint64_t fp_scvtf_d_x(int64_t a) {
  uint64_t r;
  asm volatile(
      "scvtf d0, %x[a]\n\t"
      "str d0, [%[pr]]\n\t"
      :
      : [a] "r"(a), [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

uint32_t fp_ucvtf_s_w(uint32_t a) {
  uint32_t r;
  asm volatile(
      "ucvtf s0, %w[a]\n\t"
      "str s0, [%[pr]]\n\t"
      :
      : [a] "r"(a), [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

uint64_t fp_ucvtf_d_w(uint32_t a) {
  uint64_t r;
  asm volatile(
      "ucvtf d0, %w[a]\n\t"
      "str d0, [%[pr]]\n\t"
      :
      : [a] "r"(a), [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

#define FP16_OP_1SRC(MNEMONIC)                                                 \
  uint16_t fp16_##MNEMONIC(uint16_t a) {                                       \
    uint16_t r;                                                                \
    asm volatile(                                                              \
        "ldr h1, [%[pa]]\n\t"                                                  \
        #MNEMONIC " h0, h1\n\t"                                                \
        "str h0, [%[pr]]\n\t"                                                  \
        :                                                                      \
        : [pa] "r"(&a), [pr] "r"(&r)                                           \
        : "v0", "v1", "memory");                                               \
    return r;                                                                  \
  }

FP16_OP_1SRC(fabs)
FP16_OP_1SRC(fneg)
FP16_OP_1SRC(fsqrt)
FP16_OP_1SRC(fmov)
FP16_OP_1SRC(frintn)
FP16_OP_1SRC(frintp)
FP16_OP_1SRC(frintm)
FP16_OP_1SRC(frintz)
FP16_OP_1SRC(frinta)
FP16_OP_1SRC(frintx)
FP16_OP_1SRC(frinti)

// FCVT scalar between-precisions probes (FpDataProc1 opcode 0b000100 /
// 0b000101 / 0b000111).  Each one loads a single half/single/double input
// from memory, executes the convert, and stores the result back as raw
// bits so the reference (computed via host float arithmetic) can be
// compared bit-for-bit.  The JIT lowering writes the result into lane 0
// of a zero-filled XMM and stores the full 128 bits, so the str
// instruction below sees the ARM scalar layout [val_in_low_N, 0×rest].

// FCVT Sd, Hn — half -> single.
uint32_t fp_fcvt_s_h(uint16_t a) {
  uint32_t r;
  asm volatile(
      "ldr h1, [%[pa]]\n\t"
      "fcvt s0, h1\n\t"
      "str s0, [%[pr]]\n\t"
      :
      : [pa] "r"(&a), [pr] "r"(&r)
      : "v0", "v1", "memory");
  return r;
}

// FCVT Dd, Hn — half -> double.
uint64_t fp_fcvt_d_h(uint16_t a) {
  uint64_t r;
  asm volatile(
      "ldr h1, [%[pa]]\n\t"
      "fcvt d0, h1\n\t"
      "str d0, [%[pr]]\n\t"
      :
      : [pa] "r"(&a), [pr] "r"(&r)
      : "v0", "v1", "memory");
  return r;
}

// FCVT Hd, Sn — single -> half.
uint16_t fp_fcvt_h_s(uint32_t a) {
  uint16_t r;
  asm volatile(
      "ldr s1, [%[pa]]\n\t"
      "fcvt h0, s1\n\t"
      "str h0, [%[pr]]\n\t"
      :
      : [pa] "r"(&a), [pr] "r"(&r)
      : "v0", "v1", "memory");
  return r;
}

// FCVT Hd, Dn — double -> half.
uint16_t fp_fcvt_h_d(uint64_t a) {
  uint16_t r;
  asm volatile(
      "ldr d1, [%[pa]]\n\t"
      "fcvt h0, d1\n\t"
      "str h0, [%[pr]]\n\t"
      :
      : [pa] "r"(&a), [pr] "r"(&r)
      : "v0", "v1", "memory");
  return r;
}

// FCVT Dd, Sn — single -> double.
uint64_t fp_fcvt_d_s(uint32_t a) {
  uint64_t r;
  asm volatile(
      "ldr s1, [%[pa]]\n\t"
      "fcvt d0, s1\n\t"
      "str d0, [%[pr]]\n\t"
      :
      : [pa] "r"(&a), [pr] "r"(&r)
      : "v0", "v1", "memory");
  return r;
}

// FCVT Sd, Dn — double -> single.
uint32_t fp_fcvt_s_d(uint64_t a) {
  uint32_t r;
  asm volatile(
      "ldr d1, [%[pa]]\n\t"
      "fcvt s0, d1\n\t"
      "str s0, [%[pr]]\n\t"
      :
      : [pa] "r"(&a), [pr] "r"(&r)
      : "v0", "v1", "memory");
  return r;
}

// FCSEL Hd, Hn, Hm, eq.  Use cmp w0, #1 to set Z=1 (eq) or Z=0 (ne).
uint16_t fp16_fcsel_eq(uint16_t hn, uint16_t hm, bool select_n) {
  uint16_t r;
  uint64_t cmp_arg = select_n ? 1 : 0;
  asm volatile(
      "ldr h1, [%[pa]]\n\t"
      "ldr h2, [%[pb]]\n\t"
      "cmp %[c], #1\n\t"
      "fcsel h0, h1, h2, eq\n\t"
      "str h0, [%[pr]]\n\t"
      :
      : [pa] "r"(&hn), [pb] "r"(&hm), [c] "r"(cmp_arg), [pr] "r"(&r)
      : "v0", "v1", "v2", "cc", "memory");
  return r;
}

// FMOV h0, #1.0 — immediate form, returns the H0 result bits.
uint16_t fp16_fmov_imm_one() {
  uint16_t r;
  asm volatile(
      "fmov h0, #1.0\n\t"
      "str h0, [%[pr]]\n\t"
      :
      : [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

uint16_t fp16_fmov_imm_neg_two() {
  uint16_t r;
  asm volatile(
      "fmov h0, #-2.0\n\t"
      "str h0, [%[pr]]\n\t"
      :
      : [pr] "r"(&r)
      : "v0", "memory");
  return r;
}

// FCMP Hn, Hm and FCMP Hn, #0.0 set NZCV.  Probe by reading the flags
// after the compare and packing them into a uint32_t.
uint32_t fp16_fcmp_flags(uint16_t a, uint16_t b) {
  uint64_t nzcv;
  asm volatile(
      "ldr h1, [%[pa]]\n\t"
      "ldr h2, [%[pb]]\n\t"
      "fcmp h1, h2\n\t"
      "mrs %[out], nzcv\n\t"
      : [out] "=r"(nzcv)
      : [pa] "r"(&a), [pb] "r"(&b)
      : "v1", "v2", "cc");
  return static_cast<uint32_t>(nzcv >> 28);  // bits[31:28] -> bits[3:0]
}

uint32_t fp16_fcmp_zero_flags(uint16_t a) {
  uint64_t nzcv;
  asm volatile(
      "ldr h1, [%[pa]]\n\t"
      "fcmp h1, #0.0\n\t"
      "mrs %[out], nzcv\n\t"
      : [out] "=r"(nzcv)
      : [pa] "r"(&a)
      : "v1", "cc");
  return static_cast<uint32_t>(nzcv >> 28);
}

// ============================================================================
// FP16 vector (NEON) probes — Plan §E2.
//
// Encoding template "Advanced SIMD three same (FP16)" (ARM ARM C7.2):
//   0 Q U 0 1 1 1 0 a 1 0 Rm 0 0 opcode 1 Rn Rd
//   bit23=a, bit22=1 (fixed), bit21=0 (fixed), bits[15:14]=00 (fixed),
//   bits[13:11]=3-bit opcode, bit10=1 (fixed).
// llvm-mc-verified per-op encodings (8-lane v0.8h would set bit30):
//   0x0e421420 fadd v0.4h, v1.4h, v2.4h    (a=0,U=0,op=010)
//   0x0ec21420 fsub v0.4h, v1.4h, v2.4h    (a=1,U=0,op=010)
//   0x2e421c20 fmul v0.4h, v1.4h, v2.4h    (a=0,U=1,op=011)
//   0x2e423c20 fdiv v0.4h, v1.4h, v2.4h    (a=0,U=1,op=111)
//   0x0e423420 fmax v0.4h, v1.4h, v2.4h    (a=0,U=0,op=110)
//   0x0ec23420 fmin v0.4h, v1.4h, v2.4h    (a=1,U=0,op=110)
//   0x0e420420 fmaxnm                       (a=0,U=0,op=000)
//   0x0ec20420 fminnm                       (a=1,U=0,op=000)
//   0x0e422420 fcmeq                        (a=0,U=0,op=100)
//   0x2e422420 fcmge                        (a=0,U=1,op=100)
//   0x2ec22420 fcmgt                        (a=1,U=1,op=100)
//   0x2e422c20 facge                        (a=0,U=1,op=101)
//   0x2ec22c20 facgt                        (a=1,U=1,op=101)
//   0x2ec21420 fabd                         (a=1,U=1,op=010)
//   0x0e420c20 fmla                         (a=0,U=0,op=001)
//   0x0ec20c20 fmls                         (a=1,U=0,op=001)
//
// Each probe runs the op on a 4-lane vector of FP16 inputs and compares
// each lane to a portable C reference computed via HalfToSingle -> binary32
// op -> SingleToHalf (identical to the §E1 scalar reference).

#define FP16V_OP_2SRC(MNEMONIC)                                                \
  void fp16v_##MNEMONIC(const uint16_t a[4], const uint16_t b[4],              \
                        uint16_t r[4]) {                                       \
    asm volatile(                                                              \
        "ld1 {v1.4h}, [%[pa]]\n\t"                                             \
        "ld1 {v2.4h}, [%[pb]]\n\t"                                             \
        #MNEMONIC " v0.4h, v1.4h, v2.4h\n\t"                                   \
        "st1 {v0.4h}, [%[pr]]\n\t"                                             \
        :                                                                      \
        : [pa] "r"(a), [pb] "r"(b), [pr] "r"(r)                                \
        : "v0", "v1", "v2", "memory");                                         \
  }

FP16V_OP_2SRC(fadd)
FP16V_OP_2SRC(fsub)
FP16V_OP_2SRC(fmul)
FP16V_OP_2SRC(fdiv)
FP16V_OP_2SRC(fmax)
FP16V_OP_2SRC(fmin)
FP16V_OP_2SRC(fmaxnm)
FP16V_OP_2SRC(fminnm)
FP16V_OP_2SRC(fabd)
FP16V_OP_2SRC(fcmeq)
FP16V_OP_2SRC(fcmge)
FP16V_OP_2SRC(fcmgt)
FP16V_OP_2SRC(facge)
FP16V_OP_2SRC(facgt)

// FMLA / FMLS: destination V0 is read as the accumulator, so it must be
// pre-loaded from |d|.
#define FP16V_OP_FMA(MNEMONIC)                                                 \
  void fp16v_##MNEMONIC(const uint16_t a[4], const uint16_t b[4],              \
                        const uint16_t d[4], uint16_t r[4]) {                  \
    asm volatile(                                                              \
        "ld1 {v0.4h}, [%[pd]]\n\t"                                             \
        "ld1 {v1.4h}, [%[pa]]\n\t"                                             \
        "ld1 {v2.4h}, [%[pb]]\n\t"                                             \
        #MNEMONIC " v0.4h, v1.4h, v2.4h\n\t"                                   \
        "st1 {v0.4h}, [%[pr]]\n\t"                                             \
        :                                                                      \
        : [pa] "r"(a), [pb] "r"(b), [pd] "r"(d), [pr] "r"(r)                   \
        : "v0", "v1", "v2", "memory");                                         \
  }

FP16V_OP_FMA(fmla)
FP16V_OP_FMA(fmls)

// region digitalis - Plan §E2: Armv8.2-FP16 vector two-register miscellaneous.
// llvm-mc-verified per-op encodings for v0.4h <- f(v1.4h):
//   0x0ef8f820 fabs   v0.4h, v1.4h           (a=1, U=0, op=01111)
//   0x2ef8f820 fneg   v0.4h, v1.4h           (a=1, U=1, op=01111)
//   0x2ef9f820 fsqrt  v0.4h, v1.4h           (a=1, U=1, op=11111)
//   0x0ef8c820 fcmgt  v0.4h, v1.4h, #0.0     (a=1, U=0, op=01100)
//   0x0ef8d820 fcmeq  v0.4h, v1.4h, #0.0     (a=1, U=0, op=01101)
//   0x0ef8e820 fcmlt  v0.4h, v1.4h, #0.0     (a=1, U=0, op=01110)
//   0x2ef8c820 fcmge  v0.4h, v1.4h, #0.0     (a=1, U=1, op=01100)
//   0x2ef8d820 fcmle  v0.4h, v1.4h, #0.0     (a=1, U=1, op=01101)
#define FP16V_OP_1SRC(MNEMONIC)                                                \
  void fp16v_##MNEMONIC(const uint16_t a[4], uint16_t r[4]) {                  \
    asm volatile(                                                              \
        "ld1 {v1.4h}, [%[pa]]\n\t"                                             \
        #MNEMONIC " v0.4h, v1.4h\n\t"                                          \
        "st1 {v0.4h}, [%[pr]]\n\t"                                             \
        :                                                                      \
        : [pa] "r"(a), [pr] "r"(r)                                             \
        : "v0", "v1", "memory");                                               \
  }

FP16V_OP_1SRC(fabs)
FP16V_OP_1SRC(fneg)
FP16V_OP_1SRC(fsqrt)

#define FP16V_OP_CMP0(MNEMONIC)                                                \
  void fp16v_##MNEMONIC##_zero(const uint16_t a[4], uint16_t r[4]) {           \
    asm volatile(                                                              \
        "ld1 {v1.4h}, [%[pa]]\n\t"                                             \
        #MNEMONIC " v0.4h, v1.4h, #0.0\n\t"                                    \
        "st1 {v0.4h}, [%[pr]]\n\t"                                             \
        :                                                                      \
        : [pa] "r"(a), [pr] "r"(r)                                             \
        : "v0", "v1", "memory");                                               \
  }

FP16V_OP_CMP0(fcmeq)
FP16V_OP_CMP0(fcmgt)
FP16V_OP_CMP0(fcmge)
FP16V_OP_CMP0(fcmle)
FP16V_OP_CMP0(fcmlt)

// region digitalis - Plan §E2 a=0/a=1 columns: FP16 vector two-reg-misc
// FRINT*/FCVT*/SCVTF/UCVTF/FRECPE/FRSQRTE.  All take a single .4h source
// and produce a .4h destination (either half-precision FP or int16 bit
// pattern, depending on the op).  Reuses the existing FP16V_OP_1SRC form.
FP16V_OP_1SRC(frintn)   // ties-to-even
FP16V_OP_1SRC(frinta)   // ties-away-from-zero
FP16V_OP_1SRC(frintm)   // toward -inf
FP16V_OP_1SRC(frintp)   // toward +inf
FP16V_OP_1SRC(frintz)   // toward zero
FP16V_OP_1SRC(frintx)   // current rounding mode (default = ties-to-even)
FP16V_OP_1SRC(frinti)   // current rounding mode (default = ties-to-even)
FP16V_OP_1SRC(fcvtns)   // FP16 -> int16, ties-to-even (signed)
FP16V_OP_1SRC(fcvtnu)   // FP16 -> uint16, ties-to-even
FP16V_OP_1SRC(fcvtms)   // FP16 -> int16, toward -inf
FP16V_OP_1SRC(fcvtmu)   // FP16 -> uint16, toward -inf
FP16V_OP_1SRC(fcvtps)   // FP16 -> int16, toward +inf
FP16V_OP_1SRC(fcvtpu)   // FP16 -> uint16, toward +inf
FP16V_OP_1SRC(fcvtas)   // FP16 -> int16, ties-away
FP16V_OP_1SRC(fcvtau)   // FP16 -> uint16, ties-away
FP16V_OP_1SRC(fcvtzs)   // FP16 -> int16, truncate
FP16V_OP_1SRC(fcvtzu)   // FP16 -> uint16, truncate
FP16V_OP_1SRC(scvtf)    // int16 -> FP16
FP16V_OP_1SRC(ucvtf)    // uint16 -> FP16
FP16V_OP_1SRC(frecpe)   // reciprocal estimate
FP16V_OP_1SRC(frsqrte)  // reciprocal sqrt estimate
// endregion
// endregion

// region digitalis - Plan §E2 FP16 vector indexed FMLA/FMLS/FMUL (handoff-62).
// llvm-mc-verified .inst encodings (--mattr=+fullfp16):
//   fmla v0.4h, v1.4h, v2.h[0] = 0x0f021020
//   fmla v0.4h, v1.4h, v2.h[3] = 0x0f321020   (L=1,M=1,H=0; index=H:L:M=011)
//   fmla v0.8h, v1.8h, v2.h[0] = 0x4f021020
//   fmla v0.8h, v1.8h, v2.h[7] = 0x4f321820   (L=1,M=1,H=1; index=111)
//   fmls v0.4h, v1.4h, v2.h[0] = 0x0f025020   (opcode=0101)
//   fmls v0.8h, v1.8h, v2.h[5] = 0x4f125820   (L=0,M=1,H=1; index=101)
//   fmul v0.4h, v1.4h, v2.h[1] = 0x0f129020   (opcode=1001; L=0,M=1)
//   fmul v0.8h, v1.8h, v2.h[3] = 0x4f329020   (opcode=1001; L=1,M=1,H=0)
//
// Vn / Vd are .4h or .8h per Q.  Vm is V0..V15 (4-bit register field,
// the spare M bit is consumed by the index).  Index ranges 0..7 across
// Vm.8H independent of Q.
//
// FMLA / FMLS / FMUL by element: destination is read for the accumulator
// in FMLA/FMLS, but is just an output for FMUL.  We pre-load v0 in all
// cases so FMLA/FMLS can pick up |d| as the accumulator; FMUL overwrites.
// Per-form probes (NAME, .inst encoding, 4h vs 8h flag for ld1/st1 type spec).
// .4h forms use ".4h"; .8h forms use ".8h".
#define FP16V_PROBE_4H(NAME, HEX) \
  void fp16v_##NAME(const uint16_t a[4], const uint16_t b[8],                  \
                    const uint16_t d[4], uint16_t r[4]) {                      \
    asm volatile(                                                              \
        "ld1 {v0.4h}, [%[pd]]\n\t"                                             \
        "ld1 {v1.4h}, [%[pa]]\n\t"                                             \
        "ld1 {v2.8h}, [%[pb]]\n\t"                                             \
        ".inst " #HEX "\n\t"                                                   \
        "st1 {v0.4h}, [%[pr]]\n\t"                                             \
        :                                                                      \
        : [pa] "r"(a), [pb] "r"(b), [pd] "r"(d), [pr] "r"(r)                   \
        : "v0", "v1", "v2", "memory");                                         \
  }

#define FP16V_PROBE_8H(NAME, HEX) \
  void fp16v_##NAME(const uint16_t a[8], const uint16_t b[8],                  \
                    const uint16_t d[8], uint16_t r[8]) {                      \
    asm volatile(                                                              \
        "ld1 {v0.8h}, [%[pd]]\n\t"                                             \
        "ld1 {v1.8h}, [%[pa]]\n\t"                                             \
        "ld1 {v2.8h}, [%[pb]]\n\t"                                             \
        ".inst " #HEX "\n\t"                                                   \
        "st1 {v0.8h}, [%[pr]]\n\t"                                             \
        :                                                                      \
        : [pa] "r"(a), [pb] "r"(b), [pd] "r"(d), [pr] "r"(r)                   \
        : "v0", "v1", "v2", "memory");                                         \
  }

FP16V_PROBE_4H(fmla_idx0_4h, 0x0f021020)
FP16V_PROBE_4H(fmla_idx3_4h, 0x0f321020)
FP16V_PROBE_8H(fmla_idx0_8h, 0x4f021020)
FP16V_PROBE_8H(fmla_idx7_8h, 0x4f321820)
FP16V_PROBE_4H(fmls_idx0_4h, 0x0f025020)
FP16V_PROBE_8H(fmls_idx5_8h, 0x4f125820)
FP16V_PROBE_4H(fmul_idx1_4h, 0x0f129020)
FP16V_PROBE_8H(fmul_idx3_8h, 0x4f329020)
// endregion

// 8-lane (Q=1) variant for FADD — confirms that the Q bit threads through
// the decoder (4-lane uses Q=0, 8-lane uses Q=1; same opcode/U/a bits).
void fp16v8_fadd(const uint16_t a[8], const uint16_t b[8], uint16_t r[8]) {
  asm volatile(
      "ld1 {v1.8h}, [%[pa]]\n\t"
      "ld1 {v2.8h}, [%[pb]]\n\t"
      "fadd v0.8h, v1.8h, v2.8h\n\t"
      "st1 {v0.8h}, [%[pr]]\n\t"
      :
      : [pa] "r"(a), [pb] "r"(b), [pr] "r"(r)
      : "v0", "v1", "v2", "memory");
}

bool check_vec(std::string& report, char (&buf)[256], const char* name,
               const uint16_t* actual, const uint16_t* expected, int lanes) {
  bool all_ok = true;
  for (int i = 0; i < lanes; i++) {
    bool ok = (actual[i] == expected[i]);
    if (!ok) {
      // Hush NaN payload mismatch as in scalar check().
      uint16_t a_exp = (actual[i] >> 10) & 0x1F;
      uint16_t e_exp = (expected[i] >> 10) & 0x1F;
      uint16_t a_frac = actual[i] & 0x3FF;
      uint16_t e_frac = expected[i] & 0x3FF;
      if (a_exp == 0x1F && e_exp == 0x1F && a_frac != 0 && e_frac != 0 &&
          (actual[i] & 0x8000) == (expected[i] & 0x8000)) {
        ok = true;
      }
    }
    if (!ok) all_ok = false;
  }
  std::snprintf(buf, sizeof(buf), "%-12s vec[0..%d] %s\n", name, lanes - 1,
                all_ok ? "OK" : "MISMATCH");
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
  if (!all_ok) {
    for (int i = 0; i < lanes; i++) {
      std::snprintf(buf, sizeof(buf), "  lane %d actual=0x%04x expected=0x%04x\n",
                    i, actual[i], expected[i]);
      __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
    }
  }
  return all_ok;
}

bool check(std::string& report, char (&buf)[256], const char* name,
           uint16_t actual, uint16_t expected) {
  bool ok = (actual == expected);
  // Hush NaN payload comparison: any QNaN with the right sign+exp counts as a match.
  if (!ok) {
    uint16_t a_exp = (actual >> 10) & 0x1F;
    uint16_t e_exp = (expected >> 10) & 0x1F;
    uint16_t a_frac = actual & 0x3FF;
    uint16_t e_frac = expected & 0x3FF;
    if (a_exp == 0x1F && e_exp == 0x1F && a_frac != 0 && e_frac != 0 &&
        (actual & 0x8000) == (expected & 0x8000)) {
      ok = true;
    }
  }
  std::snprintf(buf, sizeof(buf), "%-12s actual=0x%04x expected=0x%04x  %s\n",
                name, actual, expected, ok ? "OK" : "MISMATCH");
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
  return ok;
}

// Overloads for FCVT cross-precision probes that produce binary32 / binary64
// payloads.  Exact bit-equality only — finite inputs land bit-exact through
// the JIT's CVT/VCVT chain, so no NaN-payload softening is needed.
bool check(std::string& report, char (&buf)[256], const char* name,
           uint32_t actual, uint32_t expected) {
  bool ok = (actual == expected);
  std::snprintf(buf, sizeof(buf),
                "%-20s actual=0x%08x expected=0x%08x  %s\n",
                name, actual, expected, ok ? "OK" : "MISMATCH");
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
  return ok;
}

bool check(std::string& report, char (&buf)[256], const char* name,
           uint64_t actual, uint64_t expected) {
  bool ok = (actual == expected);
  std::snprintf(buf, sizeof(buf),
                "%-20s actual=0x%016llx expected=0x%016llx  %s\n",
                name, static_cast<unsigned long long>(actual),
                static_cast<unsigned long long>(expected),
                ok ? "OK" : "MISMATCH");
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);
  return ok;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellofp16_MainActivity_probeFp16(JNIEnv* env, jobject) {
  std::string report;
  char buf[256];
  int ok = 0, total = 0;

  // Inputs: a few finite values whose round-trip through binary16 is
  // non-trivial — 1.5, 3.5, 1.0/3.0 (recurring binary fraction), sqrt(2),
  // and a 0 value for FCMP edge cases.
  const uint16_t h_1p5    = SingleToHalf(1.5f);
  const uint16_t h_3p5    = SingleToHalf(3.5f);
  const uint16_t h_third  = SingleToHalf(1.0f / 3.0f);
  const uint16_t h_2p0    = SingleToHalf(2.0f);
  const uint16_t h_neg1p5 = SingleToHalf(-1.5f);
  const uint16_t h_3p25   = SingleToHalf(3.25f);
  const uint16_t h_zero   = 0x0000;
  const uint16_t h_neg2p7 = SingleToHalf(-2.7f);

  auto ref_2src = [](float (*op)(float, float), uint16_t a, uint16_t b) {
    return SingleToHalf(op(HalfToSingle(a), HalfToSingle(b)));
  };

  total++; if (check(report, buf, "FADD",   fp16_fadd  (h_1p5, h_3p5),
                     ref_2src([](float x, float y) { return x + y; }, h_1p5, h_3p5))) ok++;
  total++; if (check(report, buf, "FSUB",   fp16_fsub  (h_1p5, h_3p5),
                     ref_2src([](float x, float y) { return x - y; }, h_1p5, h_3p5))) ok++;
  total++; if (check(report, buf, "FMUL",   fp16_fmul  (h_third, h_3p5),
                     ref_2src([](float x, float y) { return x * y; }, h_third, h_3p5))) ok++;
  total++; if (check(report, buf, "FDIV",   fp16_fdiv  (h_1p5, h_3p5),
                     ref_2src([](float x, float y) { return x / y; }, h_1p5, h_3p5))) ok++;
  total++; if (check(report, buf, "FMAX",   fp16_fmax  (h_1p5, h_3p5),
                     ref_2src([](float x, float y) { return std::fmax(x, y); }, h_1p5, h_3p5))) ok++;
  total++; if (check(report, buf, "FMIN",   fp16_fmin  (h_1p5, h_3p5),
                     ref_2src([](float x, float y) { return std::fmin(x, y); }, h_1p5, h_3p5))) ok++;
  total++; if (check(report, buf, "FMAXNM", fp16_fmaxnm(h_neg1p5, h_3p5),
                     ref_2src([](float x, float y) { return std::fmax(x, y); }, h_neg1p5, h_3p5))) ok++;
  total++; if (check(report, buf, "FMINNM", fp16_fminnm(h_neg1p5, h_3p5),
                     ref_2src([](float x, float y) { return std::fmin(x, y); }, h_neg1p5, h_3p5))) ok++;
  total++; if (check(report, buf, "FNMUL",  fp16_fnmul (h_1p5, h_3p5),
                     ref_2src([](float x, float y) { return -(x * y); }, h_1p5, h_3p5))) ok++;

  // 3-source FMA family: result = fma(±n, m, ±a)
  auto ref_3src = [](float (*op)(float, float, float),
                     uint16_t a, uint16_t b, uint16_t c) {
    return SingleToHalf(op(HalfToSingle(a), HalfToSingle(b), HalfToSingle(c)));
  };
  total++; if (check(report, buf, "FMADD",  fp16_fmadd (h_1p5, h_3p5, h_2p0),
                     ref_3src([](float n, float m, float a) { return std::fma(n, m, a); },
                              h_1p5, h_3p5, h_2p0))) ok++;
  total++; if (check(report, buf, "FMSUB",  fp16_fmsub (h_1p5, h_3p5, h_2p0),
                     ref_3src([](float n, float m, float a) { return std::fma(-n, m, a); },
                              h_1p5, h_3p5, h_2p0))) ok++;
  total++; if (check(report, buf, "FNMADD", fp16_fnmadd(h_1p5, h_3p5, h_2p0),
                     ref_3src([](float n, float m, float a) { return -std::fma(n, m, a); },
                              h_1p5, h_3p5, h_2p0))) ok++;
  total++; if (check(report, buf, "FNMSUB", fp16_fnmsub(h_1p5, h_3p5, h_2p0),
                     ref_3src([](float n, float m, float a) { return std::fma(n, m, -a); },
                              h_1p5, h_3p5, h_2p0))) ok++;

  // 1-source ops.
  auto ref_1src = [](float (*op)(float), uint16_t a) {
    return SingleToHalf(op(HalfToSingle(a)));
  };
  total++; if (check(report, buf, "FABS",   fp16_fabs  (h_neg1p5),
                     ref_1src([](float x) { return std::fabs(x); }, h_neg1p5))) ok++;
  total++; if (check(report, buf, "FNEG",   fp16_fneg  (h_1p5),
                     ref_1src([](float x) { return -x; }, h_1p5))) ok++;
  total++; if (check(report, buf, "FSQRT",  fp16_fsqrt (h_2p0),
                     ref_1src([](float x) { return std::sqrt(x); }, h_2p0))) ok++;
  total++; if (check(report, buf, "FMOV",   fp16_fmov  (h_1p5), h_1p5)) ok++;
  total++; if (check(report, buf, "FRINTN", fp16_frintn(h_3p25),
                     ref_1src([](float x) { return std::nearbyint(x); }, h_3p25))) ok++;
  total++; if (check(report, buf, "FRINTP", fp16_frintp(h_neg2p7),
                     ref_1src([](float x) { return std::ceil(x); }, h_neg2p7))) ok++;
  total++; if (check(report, buf, "FRINTM", fp16_frintm(h_neg2p7),
                     ref_1src([](float x) { return std::floor(x); }, h_neg2p7))) ok++;
  total++; if (check(report, buf, "FRINTZ", fp16_frintz(h_neg2p7),
                     ref_1src([](float x) { return std::trunc(x); }, h_neg2p7))) ok++;
  total++; if (check(report, buf, "FRINTA", fp16_frinta(h_3p25),
                     ref_1src([](float x) { return std::round(x); }, h_3p25))) ok++;
  total++; if (check(report, buf, "FRINTX", fp16_frintx(h_3p25),
                     ref_1src([](float x) { return std::rint(x); }, h_3p25))) ok++;
  total++; if (check(report, buf, "FRINTI", fp16_frinti(h_3p25),
                     ref_1src([](float x) { return std::rint(x); }, h_3p25))) ok++;

  // FCVT scalar between-precisions probes — FpDataProc1 opcodes 0b000100
  // (FCVT to "other-1": H->S, D->S), 0b000101 (FCVT to "other-2": S->D,
  // H->D), 0b000111 (FCVT to half: S->H, D->H).  Reference values are
  // computed via the host's float/double arithmetic and the same
  // HalfToSingle / SingleToHalf helpers used by the FP16 scalar probes.
  // Inputs cover finite values, ±0, and an FP16 subnormal so the
  // mantissa-renormalization path is exercised at least once.
  auto u32_of_float = [](float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
  };
  auto u64_of_double = [](double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, 8);
    return bits;
  };

  // Scalar FP32 (S) and FP64 (D) FpDataProc2 probes — the same five opcodes
  // probed for FP16 above (FMAX / FMIN / FMAXNM / FMINNM / FNMUL), exercising
  // the JIT ftype=00 and ftype=01 lane-0 emit sequences added in handoff-92.
  // Inputs are finite (no NaN), so std::fmax/std::fmin match both ARM FMAX
  // (NaN-prop) and ARM FMAXNM (NaN-suppress) semantics bit-exactly.
  total++; if (check(report, buf, "FMAX.s",   u32_of_float(fp32_fmax  ( 1.5f, 3.5f)),
                     u32_of_float(std::fmax( 1.5f, 3.5f)))) ok++;
  total++; if (check(report, buf, "FMIN.s",   u32_of_float(fp32_fmin  ( 1.5f, 3.5f)),
                     u32_of_float(std::fmin( 1.5f, 3.5f)))) ok++;
  total++; if (check(report, buf, "FMAXNM.s", u32_of_float(fp32_fmaxnm(-1.5f, 3.5f)),
                     u32_of_float(std::fmax(-1.5f, 3.5f)))) ok++;
  total++; if (check(report, buf, "FMINNM.s", u32_of_float(fp32_fminnm(-1.5f, 3.5f)),
                     u32_of_float(std::fmin(-1.5f, 3.5f)))) ok++;
  total++; if (check(report, buf, "FNMUL.s",  u32_of_float(fp32_fnmul ( 1.5f, 3.5f)),
                     u32_of_float(-(1.5f * 3.5f)))) ok++;

  total++; if (check(report, buf, "FMAX.d",   u64_of_double(fp64_fmax  ( 1.5, 3.5)),
                     u64_of_double(std::fmax( 1.5, 3.5)))) ok++;
  total++; if (check(report, buf, "FMIN.d",   u64_of_double(fp64_fmin  ( 1.5, 3.5)),
                     u64_of_double(std::fmin( 1.5, 3.5)))) ok++;
  total++; if (check(report, buf, "FMAXNM.d", u64_of_double(fp64_fmaxnm(-1.5, 3.5)),
                     u64_of_double(std::fmax(-1.5, 3.5)))) ok++;
  total++; if (check(report, buf, "FMINNM.d", u64_of_double(fp64_fminnm(-1.5, 3.5)),
                     u64_of_double(std::fmin(-1.5, 3.5)))) ok++;
  total++; if (check(report, buf, "FNMUL.d",  u64_of_double(fp64_fnmul ( 1.5, 3.5)),
                     u64_of_double(-(1.5 * 3.5)))) ok++;

  // Scalar FP32 (S) / FP64 (D) FpDataProc3 probes — FMADD / FMSUB / FNMADD /
  // FNMSUB.  These hit the FMA3 JIT paths (Vfmadd231ss / Vfnmadd231ss /
  // Vfnmsub231ss / Vfmsub231ss for S; ...sd for D).  ARM semantics:
  //   FMADD   Rd = Ra + Rn*Rm    (single rounding)
  //   FMSUB   Rd = Ra - Rn*Rm
  //   FNMADD  Rd = -(Ra + Rn*Rm)
  //   FNMSUB  Rd = Rn*Rm - Ra
  // We use inputs (1.5, 2.5, 0.25) where every n*m+a, n*m-a, etc. is
  // representable exactly in both FP32 and FP64, so the JIT FMA result and
  // the host's std::fma reference are bit-identical regardless of rounding.
  {
    const float fn_s = 1.5f, fm_s = 2.5f, fa_s = 0.25f;
    const float ref_madd_s  = std::fma(fn_s, fm_s, fa_s);
    const float ref_msub_s  = std::fma(-fn_s, fm_s, fa_s);
    const float ref_nmadd_s = -std::fma(fn_s, fm_s, fa_s);
    const float ref_nmsub_s = std::fma(fn_s, fm_s, -fa_s);
    total++; if (check(report, buf, "FMADD.s",
                       u32_of_float(fp32_fmadd (fn_s, fm_s, fa_s)),
                       u32_of_float(ref_madd_s))) ok++;
    total++; if (check(report, buf, "FMSUB.s",
                       u32_of_float(fp32_fmsub (fn_s, fm_s, fa_s)),
                       u32_of_float(ref_msub_s))) ok++;
    total++; if (check(report, buf, "FNMADD.s",
                       u32_of_float(fp32_fnmadd(fn_s, fm_s, fa_s)),
                       u32_of_float(ref_nmadd_s))) ok++;
    total++; if (check(report, buf, "FNMSUB.s",
                       u32_of_float(fp32_fnmsub(fn_s, fm_s, fa_s)),
                       u32_of_float(ref_nmsub_s))) ok++;

    const double fn_d = 1.5, fm_d = 2.5, fa_d = 0.25;
    const double ref_madd_d  = std::fma(fn_d, fm_d, fa_d);
    const double ref_msub_d  = std::fma(-fn_d, fm_d, fa_d);
    const double ref_nmadd_d = -std::fma(fn_d, fm_d, fa_d);
    const double ref_nmsub_d = std::fma(fn_d, fm_d, -fa_d);
    total++; if (check(report, buf, "FMADD.d",
                       u64_of_double(fp64_fmadd (fn_d, fm_d, fa_d)),
                       u64_of_double(ref_madd_d))) ok++;
    total++; if (check(report, buf, "FMSUB.d",
                       u64_of_double(fp64_fmsub (fn_d, fm_d, fa_d)),
                       u64_of_double(ref_msub_d))) ok++;
    total++; if (check(report, buf, "FNMADD.d",
                       u64_of_double(fp64_fnmadd(fn_d, fm_d, fa_d)),
                       u64_of_double(ref_nmadd_d))) ok++;
    total++; if (check(report, buf, "FNMSUB.d",
                       u64_of_double(fp64_fnmsub(fn_d, fm_d, fa_d)),
                       u64_of_double(ref_nmsub_d))) ok++;
  }

  // SCVTF / UCVTF scalar (FpIntConversion rmode=00 op=010/011): integer to
  // FP convert.  Inputs are exactly-representable in both float and double
  // (small magnitudes plus a 23-bit-safe boundary) so the host (float)/
  // (double) cast is bit-exact reference.  Covers the JIT lowerings added
  // alongside this probe: SCVTF.{S,D} from W/X (4 forms) + UCVTF.{S,D}
  // from W (2 forms).  UCVTF from X still rides the interpreter.
  {
    auto u32_of_float = [](float f) {
      uint32_t bits; std::memcpy(&bits, &f, 4); return bits;
    };
    auto u64_of_double = [](double d) {
      uint64_t bits; std::memcpy(&bits, &d, 8); return bits;
    };
    // SCVTF Sd, Wn
    total++; if (check(report, buf, "SCVTF S<-W 7",
                       fp_scvtf_s_w(7), u32_of_float(7.0f))) ok++;
    total++; if (check(report, buf, "SCVTF S<-W -42",
                       fp_scvtf_s_w(-42), u32_of_float(-42.0f))) ok++;
    total++; if (check(report, buf, "SCVTF S<-W 0",
                       fp_scvtf_s_w(0), u32_of_float(0.0f))) ok++;
    // SCVTF Dd, Wn
    total++; if (check(report, buf, "SCVTF D<-W 7",
                       fp_scvtf_d_w(7), u64_of_double(7.0))) ok++;
    total++; if (check(report, buf, "SCVTF D<-W -42",
                       fp_scvtf_d_w(-42), u64_of_double(-42.0))) ok++;
    total++; if (check(report, buf, "SCVTF D<-W INT32_MIN",
                       fp_scvtf_d_w(INT32_MIN),
                       u64_of_double(static_cast<double>(INT32_MIN)))) ok++;
    // SCVTF Sd, Xn
    total++; if (check(report, buf, "SCVTF S<-X 7",
                       fp_scvtf_s_x(7), u32_of_float(7.0f))) ok++;
    total++; if (check(report, buf, "SCVTF S<-X -42",
                       fp_scvtf_s_x(-42), u32_of_float(-42.0f))) ok++;
    // SCVTF Dd, Xn — large in-range value that survives the int64→double
    // round trip exactly (1<<52 is representable in binary64 mantissa).
    total++; if (check(report, buf, "SCVTF D<-X 1<<52",
                       fp_scvtf_d_x(1LL << 52),
                       u64_of_double(static_cast<double>(1LL << 52)))) ok++;
    total++; if (check(report, buf, "SCVTF D<-X -(1<<52)",
                       fp_scvtf_d_x(-(1LL << 52)),
                       u64_of_double(-static_cast<double>(1LL << 52)))) ok++;
    // UCVTF Sd, Wn — value > INT32_MAX exercises the unsigned semantics.
    total++; if (check(report, buf, "UCVTF S<-W 0xFFFFFFFF",
                       fp_ucvtf_s_w(0xFFFFFFFFu),
                       u32_of_float(static_cast<float>(0xFFFFFFFFu)))) ok++;
    total++; if (check(report, buf, "UCVTF S<-W 7",
                       fp_ucvtf_s_w(7), u32_of_float(7.0f))) ok++;
    // UCVTF Dd, Wn
    total++; if (check(report, buf, "UCVTF D<-W 0xFFFFFFFF",
                       fp_ucvtf_d_w(0xFFFFFFFFu),
                       u64_of_double(static_cast<double>(0xFFFFFFFFu)))) ok++;
    total++; if (check(report, buf, "UCVTF D<-W 0",
                       fp_ucvtf_d_w(0), u64_of_double(0.0))) ok++;
  }

  // FCVT Sd, Hn (H->S).  HalfToSingle is exact (FP16 mantissa < FP32).
  total++; if (check(report, buf, "FCVT S<-H 1.5",
                     fp_fcvt_s_h(h_1p5), u32_of_float(HalfToSingle(h_1p5)))) ok++;
  total++; if (check(report, buf, "FCVT S<-H -1.5",
                     fp_fcvt_s_h(h_neg1p5), u32_of_float(HalfToSingle(h_neg1p5)))) ok++;
  total++; if (check(report, buf, "FCVT S<-H 0",
                     fp_fcvt_s_h(h_zero), u32_of_float(HalfToSingle(h_zero)))) ok++;

  // FCVT Dd, Hn (H->D).
  total++; if (check(report, buf, "FCVT D<-H 1.5",
                     fp_fcvt_d_h(h_1p5),
                     u64_of_double(static_cast<double>(HalfToSingle(h_1p5))))) ok++;
  total++; if (check(report, buf, "FCVT D<-H 3.25",
                     fp_fcvt_d_h(h_3p25),
                     u64_of_double(static_cast<double>(HalfToSingle(h_3p25))))) ok++;

  // FCVT Hd, Sn (S->H).  Use the local SingleToHalf for the reference.
  total++; if (check(report, buf, "FCVT H<-S 1.5",
                     fp_fcvt_h_s(u32_of_float(1.5f)), SingleToHalf(1.5f))) ok++;
  total++; if (check(report, buf, "FCVT H<-S 3.25",
                     fp_fcvt_h_s(u32_of_float(3.25f)), SingleToHalf(3.25f))) ok++;
  total++; if (check(report, buf, "FCVT H<-S -2.7",
                     fp_fcvt_h_s(u32_of_float(-2.7f)), SingleToHalf(-2.7f))) ok++;

  // FCVT Hd, Dn (D->H).  Narrowing twice: D->S->H matches the JIT's
  // CVTSD2SS + VCVTPS2PH chain.  Run the reference the same way.
  total++; if (check(report, buf, "FCVT H<-D 1.5",
                     fp_fcvt_h_d(u64_of_double(1.5)),
                     SingleToHalf(static_cast<float>(1.5)))) ok++;
  total++; if (check(report, buf, "FCVT H<-D 3.25",
                     fp_fcvt_h_d(u64_of_double(3.25)),
                     SingleToHalf(static_cast<float>(3.25)))) ok++;

  // FCVT Dd, Sn (S->D).  Widening is exact.
  total++; if (check(report, buf, "FCVT D<-S 1.5",
                     fp_fcvt_d_s(u32_of_float(1.5f)),
                     u64_of_double(static_cast<double>(1.5f)))) ok++;
  total++; if (check(report, buf, "FCVT D<-S -2.7",
                     fp_fcvt_d_s(u32_of_float(-2.7f)),
                     u64_of_double(static_cast<double>(-2.7f)))) ok++;

  // FCVT Sd, Dn (D->S).  Narrowing with RNE.
  total++; if (check(report, buf, "FCVT S<-D 1.5",
                     fp_fcvt_s_d(u64_of_double(1.5)),
                     u32_of_float(static_cast<float>(1.5)))) ok++;
  total++; if (check(report, buf, "FCVT S<-D 3.14159265358979",
                     fp_fcvt_s_d(u64_of_double(3.14159265358979)),
                     u32_of_float(static_cast<float>(3.14159265358979)))) ok++;

  // FCSEL Hd, Hn, Hm, eq — select_n=true sets Z=1 (eq), so picks Hn.
  total++; if (check(report, buf, "FCSEL=Hn", fp16_fcsel_eq(h_1p5, h_3p5, /*select_n=*/true), h_1p5)) ok++;
  total++; if (check(report, buf, "FCSEL=Hm", fp16_fcsel_eq(h_1p5, h_3p5, /*select_n=*/false), h_3p5)) ok++;

  // FMOV imm.  Half-precision VFPExpandImm for 1.0 = (sign=0, exp=01111, frac=0000000000)
  // = 0011 1100 0000 0000 = 0x3C00.  For -2.0 = (sign=1, exp=10000, frac=0000000000)
  // = 1100 0000 0000 0000 = 0xC000.
  total++; if (check(report, buf, "FMOV=#1",  fp16_fmov_imm_one(), 0x3C00)) ok++;
  total++; if (check(report, buf, "FMOV=#-2", fp16_fmov_imm_neg_two(), 0xC000)) ok++;

  // FCMP Hn, Hm — set NZCV.  ARM64 NZCV layout: bit 3=N, 2=Z, 1=C, 0=V.
  //  greater-than → N=0 Z=0 C=1 V=0 → 0b0010 = 0x2
  //  less-than    → N=1 Z=0 C=0 V=0 → 0b1000 = 0x8
  //  equal        → N=0 Z=1 C=1 V=0 → 0b0110 = 0x6
  total++; if (check(report, buf, "FCMP gt", static_cast<uint16_t>(fp16_fcmp_flags(h_3p5, h_1p5)), 0x2)) ok++;
  total++; if (check(report, buf, "FCMP lt", static_cast<uint16_t>(fp16_fcmp_flags(h_1p5, h_3p5)), 0x8)) ok++;
  total++; if (check(report, buf, "FCMP eq", static_cast<uint16_t>(fp16_fcmp_flags(h_1p5, h_1p5)), 0x6)) ok++;

  // FCMP Hn, #0.0
  total++; if (check(report, buf, "FCMP>0",  static_cast<uint16_t>(fp16_fcmp_zero_flags(h_1p5)), 0x2)) ok++;
  total++; if (check(report, buf, "FCMP<0",  static_cast<uint16_t>(fp16_fcmp_zero_flags(h_neg1p5)), 0x8)) ok++;
  total++; if (check(report, buf, "FCMP==0", static_cast<uint16_t>(fp16_fcmp_zero_flags(h_zero)), 0x6)) ok++;

  // -- FP16 vector NEON probes (Plan §E2).
  // Four-lane input vectors; per-lane reference computed via the same
  // HalfToSingle / SingleToHalf round-trip used by the scalar probes.
  const uint16_t vA[4] = {h_1p5,  h_3p5, h_third,  h_neg1p5};
  const uint16_t vB[4] = {h_3p5,  h_1p5, h_3p25,   h_2p0};
  const uint16_t vD[4] = {h_2p0,  h_3p25, h_1p5,    h_third};  // FMLA/FMLS accumulator
  uint16_t vR[4];
  uint16_t vE[4];

  // Compute reference by promoting each pair to binary32 and applying the op
  // there, then narrowing back to half (binary32 is exact for any single
  // FP16 op).  Inline per-op rather than using a function-pointer
  // indirection — empty-capture lambda decay to fn-ptr is technically valid
  // C++ but did not produce correct codegen here in initial debugging.

#define VREF2(I, EXPR)                                                       \
  do {                                                                       \
    float x = HalfToSingle(vA[I]);                                           \
    float y = HalfToSingle(vB[I]);                                           \
    vE[I] = SingleToHalf(EXPR);                                              \
  } while (0)
#define VREF_CMP(I, EXPR)                                                    \
  do {                                                                       \
    float x = HalfToSingle(vA[I]);                                           \
    float y = HalfToSingle(vB[I]);                                           \
    vE[I] = (EXPR) ? 0xFFFF : 0x0000;                                        \
  } while (0)
#define VREF2_ALL(EXPR) do { for (int i = 0; i < 4; i++) VREF2(i, EXPR); } while (0)
#define VCMP_ALL(EXPR)  do { for (int i = 0; i < 4; i++) VREF_CMP(i, EXPR); } while (0)

  VREF2_ALL(x + y);
  fp16v_fadd(vA, vB, vR);
  total++; if (check_vec(report, buf, "FADD.4h", vR, vE, 4)) ok++;

  VREF2_ALL(x - y);
  fp16v_fsub(vA, vB, vR);
  total++; if (check_vec(report, buf, "FSUB.4h", vR, vE, 4)) ok++;

  VREF2_ALL(x * y);
  fp16v_fmul(vA, vB, vR);
  total++; if (check_vec(report, buf, "FMUL.4h", vR, vE, 4)) ok++;

  VREF2_ALL(x / y);
  fp16v_fdiv(vA, vB, vR);
  total++; if (check_vec(report, buf, "FDIV.4h", vR, vE, 4)) ok++;

  VREF2_ALL(std::fmax(x, y));
  fp16v_fmax(vA, vB, vR);
  total++; if (check_vec(report, buf, "FMAX.4h", vR, vE, 4)) ok++;

  VREF2_ALL(std::fmin(x, y));
  fp16v_fmin(vA, vB, vR);
  total++; if (check_vec(report, buf, "FMIN.4h", vR, vE, 4)) ok++;

  VREF2_ALL(std::fmax(x, y));  // FMAXNM: inputs are non-NaN here.
  fp16v_fmaxnm(vA, vB, vR);
  total++; if (check_vec(report, buf, "FMAXNM.4h", vR, vE, 4)) ok++;

  VREF2_ALL(std::fmin(x, y));
  fp16v_fminnm(vA, vB, vR);
  total++; if (check_vec(report, buf, "FMINNM.4h", vR, vE, 4)) ok++;

  VREF2_ALL(std::fabs(x - y));
  fp16v_fabd(vA, vB, vR);
  total++; if (check_vec(report, buf, "FABD.4h", vR, vE, 4)) ok++;

  VCMP_ALL(x == y);
  fp16v_fcmeq(vA, vB, vR);
  total++; if (check_vec(report, buf, "FCMEQ.4h", vR, vE, 4)) ok++;

  VCMP_ALL(x >= y);
  fp16v_fcmge(vA, vB, vR);
  total++; if (check_vec(report, buf, "FCMGE.4h", vR, vE, 4)) ok++;

  VCMP_ALL(x > y);
  fp16v_fcmgt(vA, vB, vR);
  total++; if (check_vec(report, buf, "FCMGT.4h", vR, vE, 4)) ok++;

  VCMP_ALL(std::fabs(x) >= std::fabs(y));
  fp16v_facge(vA, vB, vR);
  total++; if (check_vec(report, buf, "FACGE.4h", vR, vE, 4)) ok++;

  VCMP_ALL(std::fabs(x) > std::fabs(y));
  fp16v_facgt(vA, vB, vR);
  total++; if (check_vec(report, buf, "FACGT.4h", vR, vE, 4)) ok++;

#undef VREF2
#undef VREF_CMP
#undef VREF2_ALL
#undef VCMP_ALL

  // FMLA: vR[i] = vD[i] + vA[i] * vB[i]; FMLS: vR[i] = vD[i] - vA[i] * vB[i].
  // Match the interpreter's binary64 fma() round-trip to compute the reference.
  for (int i = 0; i < 4; i++) {
    double r = std::fma(static_cast<double>(HalfToSingle(vA[i])),
                        static_cast<double>(HalfToSingle(vB[i])),
                        static_cast<double>(HalfToSingle(vD[i])));
    vE[i] = SingleToHalf(static_cast<float>(r));
  }
  fp16v_fmla(vA, vB, vD, vR);
  total++; if (check_vec(report, buf, "FMLA.4h", vR, vE, 4)) ok++;

  for (int i = 0; i < 4; i++) {
    double r = std::fma(-static_cast<double>(HalfToSingle(vA[i])),
                        static_cast<double>(HalfToSingle(vB[i])),
                        static_cast<double>(HalfToSingle(vD[i])));
    vE[i] = SingleToHalf(static_cast<float>(r));
  }
  fp16v_fmls(vA, vB, vD, vR);
  total++; if (check_vec(report, buf, "FMLS.4h", vR, vE, 4)) ok++;

  // region digitalis - Plan §E2: Armv8.2-FP16 vector two-reg-misc probes.
  // FABS/FNEG/FSQRT and FCMxxx-zero with .4h lanes.  Per-lane reference
  // uses the same HalfToSingle / SingleToHalf round-trip as the three-same
  // probes above.  Inputs cover positive/negative/recurring-fraction halves.
  const uint16_t vTrm[4] = {h_1p5, h_neg1p5, h_zero, h_2p0};
  uint16_t vTrmR[4];
  uint16_t vTrmE[4];

  // FABS
  for (int i = 0; i < 4; i++) {
    vTrmE[i] = SingleToHalf(std::fabs(HalfToSingle(vTrm[i])));
  }
  fp16v_fabs(vTrm, vTrmR);
  total++; if (check_vec(report, buf, "FABS.4h", vTrmR, vTrmE, 4)) ok++;

  // FNEG
  for (int i = 0; i < 4; i++) {
    vTrmE[i] = SingleToHalf(-HalfToSingle(vTrm[i]));
  }
  fp16v_fneg(vTrm, vTrmR);
  total++; if (check_vec(report, buf, "FNEG.4h", vTrmR, vTrmE, 4)) ok++;

  // FSQRT — only on non-negative inputs (FSQRT of negative is NaN; payload skip).
  const uint16_t vSqrtIn[4] = {h_1p5, h_3p5, h_2p0, h_3p25};
  uint16_t vSqrtR[4];
  uint16_t vSqrtE[4];
  for (int i = 0; i < 4; i++) {
    vSqrtE[i] = SingleToHalf(std::sqrt(HalfToSingle(vSqrtIn[i])));
  }
  fp16v_fsqrt(vSqrtIn, vSqrtR);
  total++; if (check_vec(report, buf, "FSQRT.4h", vSqrtR, vSqrtE, 4)) ok++;

  // FCMxxx zero family — five forms over the same four inputs.
  const uint16_t vCmp[4] = {h_1p5, h_neg1p5, h_zero, h_2p0};

  // FCMEQ
  for (int i = 0; i < 4; i++) {
    vTrmE[i] = (HalfToSingle(vCmp[i]) == 0.0f) ? uint16_t{0xFFFF} : uint16_t{0};
  }
  fp16v_fcmeq_zero(vCmp, vTrmR);
  total++; if (check_vec(report, buf, "FCMEQ#0.4h", vTrmR, vTrmE, 4)) ok++;

  // FCMGT
  for (int i = 0; i < 4; i++) {
    vTrmE[i] = (HalfToSingle(vCmp[i]) > 0.0f) ? uint16_t{0xFFFF} : uint16_t{0};
  }
  fp16v_fcmgt_zero(vCmp, vTrmR);
  total++; if (check_vec(report, buf, "FCMGT#0.4h", vTrmR, vTrmE, 4)) ok++;

  // FCMGE
  for (int i = 0; i < 4; i++) {
    vTrmE[i] = (HalfToSingle(vCmp[i]) >= 0.0f) ? uint16_t{0xFFFF} : uint16_t{0};
  }
  fp16v_fcmge_zero(vCmp, vTrmR);
  total++; if (check_vec(report, buf, "FCMGE#0.4h", vTrmR, vTrmE, 4)) ok++;

  // FCMLT
  for (int i = 0; i < 4; i++) {
    vTrmE[i] = (HalfToSingle(vCmp[i]) < 0.0f) ? uint16_t{0xFFFF} : uint16_t{0};
  }
  fp16v_fcmlt_zero(vCmp, vTrmR);
  total++; if (check_vec(report, buf, "FCMLT#0.4h", vTrmR, vTrmE, 4)) ok++;

  // FCMLE
  for (int i = 0; i < 4; i++) {
    vTrmE[i] = (HalfToSingle(vCmp[i]) <= 0.0f) ? uint16_t{0xFFFF} : uint16_t{0};
  }
  fp16v_fcmle_zero(vCmp, vTrmR);
  total++; if (check_vec(report, buf, "FCMLE#0.4h", vTrmR, vTrmE, 4)) ok++;
  // endregion

  // region digitalis - Plan §E2 a=0/a=1 columns: FP16 vector two-reg-misc
  // FRINT* + FCVT*/SCVTF/UCVTF + FRECPE/FRSQRTE probes.

  // FRINT* — pick inputs that distinguish all 7 rounding modes:
  //   1.5     -> N=2  A=2  M=1  P=2  Z=1  X=2  I=2
  //  -1.5     -> N=-2 A=-2 M=-2 P=-1 Z=-1 X=-2 I=-2
  //   2.5     -> N=2  A=3  M=2  P=3  Z=2  X=2  I=2
  //   3.5     -> N=4  A=4  M=3  P=4  Z=3  X=4  I=4
  const uint16_t vRint[4] = {h_1p5, h_neg1p5, SingleToHalf(2.5f), h_3p5};
  uint16_t vRintR[4];
  uint16_t vRintE[4];

#define RINT_PROBE(NAME, MNEMONIC, HOSTFN)                                    \
  for (int i = 0; i < 4; i++) {                                               \
    vRintE[i] = SingleToHalf(HOSTFN(HalfToSingle(vRint[i])));                 \
  }                                                                           \
  fp16v_##MNEMONIC(vRint, vRintR);                                            \
  total++; if (check_vec(report, buf, NAME, vRintR, vRintE, 4)) ok++

  RINT_PROBE("FRINTN.4h", frintn, nearbyintf);
  RINT_PROBE("FRINTA.4h", frinta, roundf);
  RINT_PROBE("FRINTM.4h", frintm, floorf);
  RINT_PROBE("FRINTP.4h", frintp, ceilf);
  RINT_PROBE("FRINTZ.4h", frintz, truncf);
  RINT_PROBE("FRINTX.4h", frintx, rintf);
  RINT_PROBE("FRINTI.4h", frinti, rintf);
#undef RINT_PROBE

  // FCVT* (half → int): produces int16 bit pattern in .4h lanes.
  // Use the same inputs as FRINT*; reference is identical to the
  // interpreter's saturating apply_round path.
  uint16_t vFcvtR[4];
  uint16_t vFcvtE[4];

#define FCVT_PROBE_S(NAME, MNEMONIC, HOSTFN)                                   \
  for (int i = 0; i < 4; i++) {                                                \
    float f = HalfToSingle(vRint[i]);                                          \
    int16_t v;                                                                 \
    if (f != f) { v = 0; }                                                     \
    else { double r = HOSTFN((double)f);                                       \
           if (r >= 32768.0) v = 0x7fff;                                       \
           else if (r < -32768.0) v = (int16_t)0x8000;                         \
           else v = (int16_t)r; }                                              \
    std::memcpy(&vFcvtE[i], &v, 2);                                            \
  }                                                                            \
  fp16v_##MNEMONIC(vRint, vFcvtR);                                             \
  total++; if (check_vec(report, buf, NAME, vFcvtR, vFcvtE, 4)) ok++

#define FCVT_PROBE_U(NAME, MNEMONIC, HOSTFN)                                   \
  for (int i = 0; i < 4; i++) {                                                \
    float f = HalfToSingle(vRintU[i]);                                         \
    uint16_t v;                                                                \
    if (f != f || f < 0.0f) { v = 0; }                                         \
    else { double r = HOSTFN((double)f);                                       \
           if (r >= 65536.0) v = 0xffff;                                       \
           else if (r < 0.0) v = 0;                                            \
           else v = (uint16_t)r; }                                             \
    std::memcpy(&vFcvtE[i], &v, 2);                                            \
  }                                                                            \
  fp16v_##MNEMONIC(vRintU, vFcvtR);                                            \
  total++; if (check_vec(report, buf, NAME, vFcvtR, vFcvtE, 4)) ok++

  FCVT_PROBE_S("FCVTNS.4h", fcvtns, rint);
  FCVT_PROBE_S("FCVTMS.4h", fcvtms, floor);
  FCVT_PROBE_S("FCVTPS.4h", fcvtps, ceil);
  FCVT_PROBE_S("FCVTAS.4h", fcvtas, round);
  FCVT_PROBE_S("FCVTZS.4h", fcvtzs, trunc);

  // Unsigned FCVT*: inputs are all non-negative.
  const uint16_t vRintU[4] = {h_1p5, h_3p5, SingleToHalf(2.5f), h_2p0};
  FCVT_PROBE_U("FCVTNU.4h", fcvtnu, rint);
  FCVT_PROBE_U("FCVTMU.4h", fcvtmu, floor);
  FCVT_PROBE_U("FCVTPU.4h", fcvtpu, ceil);
  FCVT_PROBE_U("FCVTAU.4h", fcvtau, round);
  FCVT_PROBE_U("FCVTZU.4h", fcvtzu, trunc);
#undef FCVT_PROBE_S
#undef FCVT_PROBE_U

  // SCVTF (int16 → half) and UCVTF (uint16 → half).  Source lanes
  // hold int16 / uint16 bit patterns; destination is half-precision FP.
  {
    int16_t s_src[4] = {-3, 0, 100, 1000};
    uint16_t vSrcS[4];
    std::memcpy(vSrcS, s_src, sizeof(s_src));
    uint16_t vScvtR[4];
    uint16_t vScvtE[4];
    for (int i = 0; i < 4; i++) {
      vScvtE[i] = SingleToHalf(static_cast<float>(s_src[i]));
    }
    fp16v_scvtf(vSrcS, vScvtR);
    total++; if (check_vec(report, buf, "SCVTF.4h", vScvtR, vScvtE, 4)) ok++;

    uint16_t u_src[4] = {0, 100, 1000, 32767};
    for (int i = 0; i < 4; i++) {
      vScvtE[i] = SingleToHalf(static_cast<float>(u_src[i]));
    }
    fp16v_ucvtf(u_src, vScvtR);
    total++; if (check_vec(report, buf, "UCVTF.4h", vScvtR, vScvtE, 4)) ok++;
  }

  // FRECPE / FRSQRTE — interpreter computes 1/x and 1/sqrt(x) in full
  // precision (the ARM spec only requires ~8 mantissa bits of accuracy,
  // but exceeding that is allowed and harmless for Newton-Raphson seeds).
  // Reference matches what the interpreter emits.
  {
    const uint16_t vRecpIn[4] = {h_1p5, h_2p0, h_3p25, h_3p5};
    uint16_t vRecpR[4];
    uint16_t vRecpE[4];
    for (int i = 0; i < 4; i++) {
      float f = HalfToSingle(vRecpIn[i]);
      vRecpE[i] = SingleToHalf(1.0f / f);
    }
    fp16v_frecpe(vRecpIn, vRecpR);
    total++; if (check_vec(report, buf, "FRECPE.4h", vRecpR, vRecpE, 4)) ok++;

    for (int i = 0; i < 4; i++) {
      float f = HalfToSingle(vRecpIn[i]);
      vRecpE[i] = SingleToHalf(1.0f / std::sqrt(f));
    }
    fp16v_frsqrte(vRecpIn, vRecpR);
    total++; if (check_vec(report, buf, "FRSQRTE.4h", vRecpR, vRecpE, 4)) ok++;
  }
  // endregion

  // 8-lane FADD: Q=1 path through the decoder.
  const uint16_t v8A[8] = {h_1p5, h_3p5, h_third, h_neg1p5, h_2p0,    h_3p25, h_zero,  h_neg2p7};
  const uint16_t v8B[8] = {h_3p5, h_1p5, h_3p25,  h_2p0,    h_neg1p5, h_third, h_3p5,  h_zero};
  uint16_t v8R[8];
  uint16_t v8E[8];
  for (int i = 0; i < 8; i++) {
    v8E[i] = SingleToHalf(HalfToSingle(v8A[i]) + HalfToSingle(v8B[i]));
  }
  fp16v8_fadd(v8A, v8B, v8R);
  total++; if (check_vec(report, buf, "FADD.8h", v8R, v8E, 8)) ok++;

  // region digitalis - Plan §E2 FP16 vector indexed FMLA/FMLS/FMUL probes.
  // Reference uses std::fma in binary64 for FMLA/FMLS (matching the
  // interpreter's round-trip), and plain binary32 multiply for FMUL.
  // The broadcast lane is a fully-populated Vm.8H regardless of Q.
  //
  // Use a single shared Vm.8H source so the index value matters: each
  // probe broadcasts Vm[index] and applies it to all output lanes.
  {
    const uint16_t vBidx[8] = {h_1p5, h_3p5, h_neg1p5, h_2p0,
                               h_3p25, h_third, h_neg2p7, h_zero};

    // FMLA .4h idx=0 → Vm[0]=1.5, mul-acc into 4 lanes of |a|.
    {
      const uint16_t vA[4] = {h_3p5, h_2p0, h_third, h_3p25};
      const uint16_t vD[4] = {h_1p5, h_neg1p5, h_2p0, h_zero};
      uint16_t vR[4], vE[4];
      const float m = HalfToSingle(vBidx[0]);
      for (int i = 0; i < 4; i++) {
        double r = std::fma(static_cast<double>(HalfToSingle(vA[i])),
                            static_cast<double>(m),
                            static_cast<double>(HalfToSingle(vD[i])));
        vE[i] = SingleToHalf(static_cast<float>(r));
      }
      fp16v_fmla_idx0_4h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMLA.4h[0]", vR, vE, 4)) ok++;
    }

    // FMLA .4h idx=3 → Vm[3]=2.0.
    {
      const uint16_t vA[4] = {h_1p5, h_neg1p5, h_2p0, h_3p5};
      const uint16_t vD[4] = {h_zero, h_3p25, h_third, h_neg2p7};
      uint16_t vR[4], vE[4];
      const float m = HalfToSingle(vBidx[3]);
      for (int i = 0; i < 4; i++) {
        double r = std::fma(static_cast<double>(HalfToSingle(vA[i])),
                            static_cast<double>(m),
                            static_cast<double>(HalfToSingle(vD[i])));
        vE[i] = SingleToHalf(static_cast<float>(r));
      }
      fp16v_fmla_idx3_4h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMLA.4h[3]", vR, vE, 4)) ok++;
    }

    // FMLA .8h idx=0 → 8 lanes accumulate Vm[0]=1.5.
    {
      const uint16_t vA[8] = {h_3p5, h_2p0, h_third, h_3p25,
                              h_1p5, h_neg1p5, h_zero, h_neg2p7};
      const uint16_t vD[8] = {h_1p5, h_neg1p5, h_2p0, h_zero,
                              h_3p5, h_3p25, h_third, h_neg2p7};
      uint16_t vR[8], vE[8];
      const float m = HalfToSingle(vBidx[0]);
      for (int i = 0; i < 8; i++) {
        double r = std::fma(static_cast<double>(HalfToSingle(vA[i])),
                            static_cast<double>(m),
                            static_cast<double>(HalfToSingle(vD[i])));
        vE[i] = SingleToHalf(static_cast<float>(r));
      }
      fp16v_fmla_idx0_8h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMLA.8h[0]", vR, vE, 8)) ok++;
    }

    // FMLA .8h idx=7 → Vm[7]=0.
    {
      const uint16_t vA[8] = {h_1p5, h_3p5, h_third, h_neg1p5,
                              h_2p0, h_3p25, h_zero, h_neg2p7};
      const uint16_t vD[8] = {h_1p5, h_3p5, h_third, h_neg1p5,
                              h_2p0, h_3p25, h_zero, h_neg2p7};
      uint16_t vR[8], vE[8];
      const float m = HalfToSingle(vBidx[7]);
      for (int i = 0; i < 8; i++) {
        double r = std::fma(static_cast<double>(HalfToSingle(vA[i])),
                            static_cast<double>(m),
                            static_cast<double>(HalfToSingle(vD[i])));
        vE[i] = SingleToHalf(static_cast<float>(r));
      }
      fp16v_fmla_idx7_8h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMLA.8h[7]", vR, vE, 8)) ok++;
    }

    // FMLS .4h idx=0 → vD - vA * Vm[0].
    {
      const uint16_t vA[4] = {h_3p5, h_2p0, h_third, h_3p25};
      const uint16_t vD[4] = {h_1p5, h_neg1p5, h_2p0, h_zero};
      uint16_t vR[4], vE[4];
      const float m = HalfToSingle(vBidx[0]);
      for (int i = 0; i < 4; i++) {
        double r = std::fma(-static_cast<double>(HalfToSingle(vA[i])),
                            static_cast<double>(m),
                            static_cast<double>(HalfToSingle(vD[i])));
        vE[i] = SingleToHalf(static_cast<float>(r));
      }
      fp16v_fmls_idx0_4h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMLS.4h[0]", vR, vE, 4)) ok++;
    }

    // FMLS .8h idx=5 → Vm[5]=1/3.
    {
      const uint16_t vA[8] = {h_1p5, h_3p5, h_third, h_neg1p5,
                              h_2p0, h_3p25, h_zero, h_neg2p7};
      const uint16_t vD[8] = {h_zero, h_3p25, h_third, h_neg2p7,
                              h_1p5, h_3p5, h_third, h_neg1p5};
      uint16_t vR[8], vE[8];
      const float m = HalfToSingle(vBidx[5]);
      for (int i = 0; i < 8; i++) {
        double r = std::fma(-static_cast<double>(HalfToSingle(vA[i])),
                            static_cast<double>(m),
                            static_cast<double>(HalfToSingle(vD[i])));
        vE[i] = SingleToHalf(static_cast<float>(r));
      }
      fp16v_fmls_idx5_8h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMLS.8h[5]", vR, vE, 8)) ok++;
    }

    // FMUL .4h idx=1 → vA * Vm[1]=3.5 (no accumulator).
    {
      const uint16_t vA[4] = {h_1p5, h_2p0, h_zero, h_neg1p5};
      const uint16_t vD[4] = {h_neg2p7, h_neg2p7, h_neg2p7, h_neg2p7};  // ignored
      uint16_t vR[4], vE[4];
      const float m = HalfToSingle(vBidx[1]);
      for (int i = 0; i < 4; i++) {
        vE[i] = SingleToHalf(HalfToSingle(vA[i]) * m);
      }
      fp16v_fmul_idx1_4h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMUL.4h[1]", vR, vE, 4)) ok++;
    }

    // FMUL .8h idx=3 → Vm[3]=2.0.
    {
      const uint16_t vA[8] = {h_1p5, h_3p5, h_third, h_neg1p5,
                              h_2p0, h_3p25, h_zero, h_neg2p7};
      const uint16_t vD[8] = {h_neg2p7, h_neg2p7, h_neg2p7, h_neg2p7,
                              h_neg2p7, h_neg2p7, h_neg2p7, h_neg2p7};  // ignored
      uint16_t vR[8], vE[8];
      const float m = HalfToSingle(vBidx[3]);
      for (int i = 0; i < 8; i++) {
        vE[i] = SingleToHalf(HalfToSingle(vA[i]) * m);
      }
      fp16v_fmul_idx3_8h(vA, vBidx, vD, vR);
      total++; if (check_vec(report, buf, "FMUL.8h[3]", vR, vE, 8)) ok++;
    }
  }
  // endregion

  std::snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", ok, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", buf);

  return env->NewStringUTF(report.c_str());
}
