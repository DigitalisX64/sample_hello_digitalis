// Integration-level probe for ARMv8 widening multiplies
// SMULL / UMULL / PMULL / PMULL2 (rows 1-2).
//
// The interpreter (interpreter.h::AdvSimdThreeDiff) implements SMULL,
// UMULL, and PMULL (including PMULL64) for every Vd shape.  This sample
// exercises every encoding to verify the decoder dispatches each form
// correctly and the interpreter produces the right numeric result.
//
// 16 probes (8 widening-multiply + 6 polynomial-multiply + 2 PMULL64):
//
//   SMULL  V.8H, Vn.8B,  Vm.8B           size=00 Q=0 U=0  opcode=1100
//   SMULL2 V.8H, Vn.16B, Vm.16B          size=00 Q=1 U=0
//   UMULL  V.8H, Vn.8B,  Vm.8B           size=00 Q=0 U=1
//   UMULL2 V.8H, Vn.16B, Vm.16B          size=00 Q=1 U=1
//   SMULL  V.4S, Vn.4H,  Vm.4H           size=01 Q=0 U=0
//   SMULL2 V.4S, Vn.8H,  Vm.8H           size=01 Q=1 U=0
//   UMULL  V.4S, Vn.4H,  Vm.4H           size=01 Q=0 U=1
//   UMULL2 V.4S, Vn.8H,  Vm.8H           size=01 Q=1 U=1
//   SMULL  V.2D, Vn.2S,  Vm.2S           size=10 Q=0 U=0
//   SMULL2 V.2D, Vn.4S,  Vm.4S           size=10 Q=1 U=0
//   UMULL  V.2D, Vn.2S,  Vm.2S           size=10 Q=0 U=1
//   UMULL2 V.2D, Vn.4S,  Vm.4S           size=10 Q=1 U=1
//   PMULL  V.8H, Vn.8B,  Vm.8B           size=00 Q=0 U=0  opcode=1110
//   PMULL2 V.8H, Vn.16B, Vm.16B          size=00 Q=1 U=0
//   PMULL  V.1Q, Vn.1D,  Vm.1D           size=11 Q=0 U=0  (PMULL64)
//   PMULL2 V.1Q, Vn.2D,  Vm.2D           size=11 Q=1 U=0
//
// llvm-mc-verified encodings (clang --target=aarch64 -march=armv8-a+aes):
//   0x0e22c020  smull  v0.8h, v1.8b,  v2.8b
//   0x4e22c020  smull2 v0.8h, v1.16b, v2.16b
//   0x2e22c020  umull  v0.8h, v1.8b,  v2.8b
//   0x6e22c020  umull2 v0.8h, v1.16b, v2.16b
//   0x0e62c020  smull  v0.4s, v1.4h,  v2.4h
//   0x4e62c020  smull2 v0.4s, v1.8h,  v2.8h
//   0x2e62c020  umull  v0.4s, v1.4h,  v2.4h
//   0x6e62c020  umull2 v0.4s, v1.8h,  v2.8h
//   0x0ea2c020  smull  v0.2d, v1.2s,  v2.2s
//   0x4ea2c020  smull2 v0.2d, v1.4s,  v2.4s
//   0x2ea2c020  umull  v0.2d, v1.2s,  v2.2s
//   0x6ea2c020  umull2 v0.2d, v1.4s,  v2.4s
//   0x0e22e020  pmull  v0.8h, v1.8b,  v2.8b
//   0x4e22e020  pmull2 v0.8h, v1.16b, v2.16b
//   0x0ee2e020  pmull  v0.1q, v1.1d,  v2.1d
//   0x4ee2e020  pmull2 v0.1q, v1.2d,  v2.2d
//
// Inputs deliberately include negative values (0x80, 0xFF, 0x8000,
// 0x80000000) so that a SMULL routed as UMULL (sign- vs zero-extension
// bug) produces a clearly wrong numerical result.  PMULL probes use
// inputs whose carry-less product differs from the integer product so a
// dispatch routing PMULL through SMULL/UMULL is caught immediately.

#include <android/log.h>
#include <jni.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#define LOG_TAG "hellowidemul"

namespace {

// Polynomial multiply reference: a*b in GF(2)[x] truncated to in_bits*2.
__uint128_t poly_mul_ref(uint64_t a, uint64_t b, unsigned in_bits) {
  __uint128_t res = 0;
  __uint128_t aa = a;
  for (unsigned i = 0; i < in_bits; ++i) {
    if ((b >> i) & 1u) res ^= (aa << i);
  }
  return res;
}

// ----------------------- 8B -> 8H widening multiply -----------------------

template <bool is_signed, bool q>
bool probe_mull_8h(std::string& report, char (&buf)[256]) {
  // 16-byte inputs; Q=0 uses lower 8B, Q=1 uses upper 8B.
  alignas(16) uint8_t n[16] = {
      0x01, 0x02, 0x7F, 0x80, 0xFF, 0xFE, 0x10, 0x20,
      0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08,
  };
  alignas(16) uint8_t m[16] = {
      0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xdead, 0xdead, 0xdead,
                                 0xdead, 0xdead, 0xdead, 0xdead};
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e22c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e22c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e22c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e22c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  // Reference: 8 lanes, byte-element-wise multiply, with sign or zero
  // extension to int/uint and truncation back to 16 bits.
  uint16_t want[8];
  uint8_t off = q ? 8 : 0;
  for (int i = 0; i < 8; i++) {
    if constexpr (is_signed) {
      int32_t a = static_cast<int8_t>(n[off + i]);
      int32_t b = static_cast<int8_t>(m[off + i]);
      want[i] = static_cast<uint16_t>(a * b);
    } else {
      uint32_t a = n[off + i];
      uint32_t b = m[off + i];
      want[i] = static_cast<uint16_t>(a * b);
    }
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) if (out[i] != want[i]) ok = false;
  snprintf(buf, sizeof(buf),
           "  %s%s .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]"
           " want=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           is_signed ? "SMULL" : "UMULL", q ? "2" : " ",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           want[0], want[1], want[2], want[3], want[4], want[5], want[6], want[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// ----------------------- 4H -> 4S widening multiply -----------------------

template <bool is_signed, bool q>
bool probe_mull_4s(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {
      0x0001, 0x7FFF, 0x8000, 0xFFFF,
      0x8001, 0x0010, 0x0100, 0x1000,
  };
  alignas(16) uint16_t m[8] = {
      0x0003, 0x0005, 0x000A, 0x0007,
      0x0011, 0x0013, 0x0017, 0x001D,
  };
  alignas(16) uint32_t out[4] = {0xdead, 0xdead, 0xdead, 0xdead};
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e62c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e62c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e62c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e62c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint32_t want[4];
  uint8_t off = q ? 4 : 0;
  for (int i = 0; i < 4; i++) {
    if constexpr (is_signed) {
      int64_t a = static_cast<int16_t>(n[off + i]);
      int64_t b = static_cast<int16_t>(m[off + i]);
      want[i] = static_cast<uint32_t>(a * b);
    } else {
      uint64_t a = n[off + i];
      uint64_t b = m[off + i];
      want[i] = static_cast<uint32_t>(a * b);
    }
  }
  bool ok = true;
  for (int i = 0; i < 4; i++) if (out[i] != want[i]) ok = false;
  snprintf(buf, sizeof(buf),
           "  %s%s .4S out=[%08x %08x %08x %08x]"
           " want=[%08x %08x %08x %08x]: %s\n",
           is_signed ? "SMULL" : "UMULL", q ? "2" : " ",
           out[0], out[1], out[2], out[3],
           want[0], want[1], want[2], want[3],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// ----------------------- 2S -> 2D widening multiply -----------------------

template <bool is_signed, bool q>
bool probe_mull_2d(std::string& report, char (&buf)[256]) {
  alignas(16) uint32_t n[4] = {0x00010001u, 0x80000000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
  alignas(16) uint32_t m[4] = {0x00000003u, 0x80000000u, 0x00000005u, 0x80000001u};
  alignas(16) uint64_t out[2] = {0xdeadbeefcafebabe, 0xdeadbeefcafebabe};
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0ea2c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4ea2c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2ea2c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6ea2c020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint64_t want[2];
  uint8_t off = q ? 2 : 0;
  for (int i = 0; i < 2; i++) {
    if constexpr (is_signed) {
      int64_t a = static_cast<int32_t>(n[off + i]);
      int64_t b = static_cast<int32_t>(m[off + i]);
      want[i] = static_cast<uint64_t>(a * b);
    } else {
      uint64_t a = n[off + i];
      uint64_t b = m[off + i];
      want[i] = a * b;
    }
  }
  bool ok = (out[0] == want[0]) && (out[1] == want[1]);
  snprintf(buf, sizeof(buf),
           "  %s%s .2D out=[%016llx %016llx] want=[%016llx %016llx]: %s\n",
           is_signed ? "SMULL" : "UMULL", q ? "2" : " ",
           (unsigned long long)out[0], (unsigned long long)out[1],
           (unsigned long long)want[0], (unsigned long long)want[1],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// ----------------------- 8B -> 8H polynomial multiply (PMULL/PMULL2) -----

template <bool q>
bool probe_pmull_8h(std::string& report, char (&buf)[256]) {
  alignas(16) uint8_t n[16] = {
      0x01, 0x02, 0x7F, 0x80, 0xFF, 0xFE, 0x10, 0x20,
      0x81, 0x82, 0x83, 0x84, 0x05, 0x06, 0x07, 0x08,
  };
  alignas(16) uint8_t m[16] = {
      0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  };
  alignas(16) uint16_t out[8] = {0xdead, 0xdead, 0xdead, 0xdead,
                                 0xdead, 0xdead, 0xdead, 0xdead};
  if constexpr (!q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e22e020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e22e020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint16_t want[8];
  uint8_t off = q ? 8 : 0;
  for (int i = 0; i < 8; i++) {
    want[i] = static_cast<uint16_t>(poly_mul_ref(n[off + i], m[off + i], 8));
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) if (out[i] != want[i]) ok = false;
  snprintf(buf, sizeof(buf),
           "  PMULL%s .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]"
           " want=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           q ? "2" : " ",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           want[0], want[1], want[2], want[3], want[4], want[5], want[6], want[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// ----------------------- 1D -> 1Q polynomial multiply (PMULL64) ----------

template <bool q>
bool probe_pmull_1q(std::string& report, char (&buf)[256]) {
  // Two 8-byte halves of each q-register.  Q=0 picks the low half;
  // Q=1 picks the high half.  Distinctive bit patterns so a wrong
  // half pick is obvious.
  alignas(16) uint64_t n[2] = {0x0123456789abcdefULL, 0xfedcba9876543210ULL};
  alignas(16) uint64_t m[2] = {0x00000000ff00ff00ULL, 0xdeadbeefcafebabeULL};
  alignas(16) uint64_t out[2] = {0xdeadbeefdeadbeefULL, 0xdeadbeefdeadbeefULL};
  if constexpr (!q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0ee2e020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4ee2e020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint8_t off = q ? 1 : 0;
  __uint128_t want = poly_mul_ref(n[off], m[off], 64);
  uint64_t want_lo = static_cast<uint64_t>(want);
  uint64_t want_hi = static_cast<uint64_t>(want >> 64);
  bool ok = (out[0] == want_lo) && (out[1] == want_hi);
  snprintf(buf, sizeof(buf),
           "  PMULL%s .1Q out=[%016llx %016llx] want=[%016llx %016llx]: %s\n",
           q ? "2" : " ",
           (unsigned long long)out[0], (unsigned long long)out[1],
           (unsigned long long)want_lo, (unsigned long long)want_hi,
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// ----------------------- Widening add/sub probes -------------------------
//
// SADDL/UADDL/SSUBL/USUBL    — both operands narrow, widen and add/sub.
// SADDW/UADDW/SSUBW/USUBW    — Vn already wide; widen Vm only, then add/sub.
// SABDL/UABDL/SABAL/UABAL    — abs difference (widening), with optional
//                              accumulate into Vd.
// One template per (family, lane-size); the (is_signed, q) template params
// select between the 4 encodings (U×Q) at compile time.

template <bool is_signed, bool q>
bool probe_addl_8h(std::string& report, char (&buf)[256]) {
  alignas(16) uint8_t n[16] = {
      0x01, 0x7F, 0x80, 0xFF, 0x10, 0x20, 0x30, 0x40,
      0x81, 0x82, 0x83, 0x84, 0xC0, 0xD0, 0xE0, 0xF0,
  };
  alignas(16) uint8_t m[16] = {
      0x02, 0x7F, 0x80, 0xFF, 0x05, 0x06, 0x07, 0x08,
      0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
  };
  alignas(16) uint16_t out[8] = {};
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e220020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e220020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e220020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e220020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint16_t want[8];
  uint8_t off = q ? 8 : 0;
  for (int i = 0; i < 8; i++) {
    if constexpr (is_signed) {
      want[i] = static_cast<uint16_t>(static_cast<int8_t>(n[off + i]) +
                                      static_cast<int8_t>(m[off + i]));
    } else {
      want[i] = static_cast<uint16_t>(static_cast<uint16_t>(n[off + i]) +
                                      static_cast<uint16_t>(m[off + i]));
    }
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) if (out[i] != want[i]) ok = false;
  snprintf(buf, sizeof(buf),
           "  %sADDL%s .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           is_signed ? "S" : "U", q ? "2" : " ",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

template <bool is_signed, bool q>
bool probe_subl_4s(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {0x0001, 0x7FFF, 0x8000, 0xFFFF,
                                0x0500, 0x6000, 0xF000, 0x1234};
  alignas(16) uint16_t m[8] = {0x0002, 0x0010, 0x0100, 0x0001,
                                0x1234, 0x5678, 0x9ABC, 0xDEF0};
  alignas(16) uint32_t out[4] = {};
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e622020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e622020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e622020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e622020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint32_t want[4];
  uint8_t off = q ? 4 : 0;
  for (int i = 0; i < 4; i++) {
    if constexpr (is_signed) {
      want[i] = static_cast<uint32_t>(static_cast<int16_t>(n[off + i]) -
                                      static_cast<int16_t>(m[off + i]));
    } else {
      want[i] = static_cast<uint32_t>(n[off + i]) -
                static_cast<uint32_t>(m[off + i]);
    }
  }
  bool ok = (out[0] == want[0] && out[1] == want[1] &&
             out[2] == want[2] && out[3] == want[3]);
  snprintf(buf, sizeof(buf),
           "  %sSUBL%s .4S out=[%08x %08x %08x %08x]: %s\n",
           is_signed ? "S" : "U", q ? "2" : " ",
           out[0], out[1], out[2], out[3], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

template <bool is_signed, bool q>
bool probe_addl_2d(std::string& report, char (&buf)[256]) {
  alignas(16) uint32_t n[4] = {0x00000001u, 0x80000000u, 0x7FFFFFFFu, 0xFFFFFFFFu};
  alignas(16) uint32_t m[4] = {0x00000002u, 0x80000000u, 0x00000005u, 0xFFFFFFFFu};
  alignas(16) uint64_t out[2] = {};
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0ea20020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4ea20020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2ea20020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6ea20020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint64_t want[2];
  uint8_t off = q ? 2 : 0;
  for (int i = 0; i < 2; i++) {
    if constexpr (is_signed) {
      want[i] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(n[off + i])) +
                                      static_cast<int64_t>(static_cast<int32_t>(m[off + i])));
    } else {
      want[i] = static_cast<uint64_t>(n[off + i]) + static_cast<uint64_t>(m[off + i]);
    }
  }
  bool ok = (out[0] == want[0]) && (out[1] == want[1]);
  snprintf(buf, sizeof(buf),
           "  %sADDL%s .2D out=[%016llx %016llx]: %s\n",
           is_signed ? "S" : "U", q ? "2" : " ",
           (unsigned long long)out[0], (unsigned long long)out[1],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// SADDW/UADDW/SSUBW/USUBW — Vn is already 4S (4× int32 lanes), Vm is .4H or .8H.
template <bool is_signed, bool q, bool is_sub>
bool probe_addsubw_4s(std::string& report, char (&buf)[256]) {
  alignas(16) uint32_t n[4] = {0x80000000u, 0x00000001u, 0xDEADBEEFu, 0x12345678u};
  alignas(16) uint16_t m[8] = {0x0001, 0x7FFF, 0x8000, 0xFFFF,
                                0x0F00, 0x1000, 0xC000, 0xABCD};
  alignas(16) uint32_t out[4] = {};
  if constexpr (!is_sub && is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e621020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_sub && is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e621020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_sub && !is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e621020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_sub && !is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e621020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_sub && is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e623020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_sub && is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e623020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_sub && !is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e623020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e623020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint32_t want[4];
  uint8_t off = q ? 4 : 0;
  for (int i = 0; i < 4; i++) {
    int64_t mm;
    if constexpr (is_signed) {
      mm = static_cast<int16_t>(m[off + i]);
    } else {
      mm = static_cast<uint16_t>(m[off + i]);
    }
    if constexpr (is_sub) {
      want[i] = static_cast<uint32_t>(static_cast<int32_t>(n[i]) - mm);
    } else {
      want[i] = static_cast<uint32_t>(static_cast<int32_t>(n[i]) + mm);
    }
  }
  bool ok = (out[0] == want[0] && out[1] == want[1] &&
             out[2] == want[2] && out[3] == want[3]);
  snprintf(buf, sizeof(buf),
           "  %s%sW%s .4S out=[%08x %08x %08x %08x]: %s\n",
           is_signed ? "S" : "U", is_sub ? "SUB" : "ADD", q ? "2" : " ",
           out[0], out[1], out[2], out[3], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// SABDL/UABDL — absolute difference (widening). 8H form (size=00).
template <bool is_signed, bool q>
bool probe_abdl_8h(std::string& report, char (&buf)[256]) {
  alignas(16) uint8_t n[16] = {
      0x01, 0x7F, 0xFF, 0x80, 0x10, 0x20, 0x30, 0x40,
      0x81, 0x82, 0x83, 0x84, 0xC0, 0xD0, 0xE0, 0xF0,
  };
  alignas(16) uint8_t m[16] = {
      0x02, 0xFF, 0x01, 0x7F, 0x40, 0x10, 0x35, 0x20,
      0x05, 0x12, 0x73, 0x84, 0x15, 0x16, 0x17, 0x18,
  };
  alignas(16) uint16_t out[8] = {};
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e227020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e227020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e227020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e227020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint16_t want[8];
  uint8_t off = q ? 8 : 0;
  for (int i = 0; i < 8; i++) {
    int32_t diff;
    if constexpr (is_signed) {
      diff = static_cast<int8_t>(n[off + i]) - static_cast<int8_t>(m[off + i]);
    } else {
      diff = static_cast<uint8_t>(n[off + i]) - static_cast<uint8_t>(m[off + i]);
    }
    want[i] = static_cast<uint16_t>(diff < 0 ? -diff : diff);
  }
  bool ok = true;
  for (int i = 0; i < 8; i++) if (out[i] != want[i]) ok = false;
  snprintf(buf, sizeof(buf),
           "  %sABDL%s .8H out=[%04x %04x %04x %04x %04x %04x %04x %04x]: %s\n",
           is_signed ? "S" : "U", q ? "2" : " ",
           out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7],
           ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

// SABAL/UABAL — abs difference plus accumulate into Vd. 4S form (size=01).
template <bool is_signed, bool q>
bool probe_abal_4s(std::string& report, char (&buf)[256]) {
  alignas(16) uint16_t n[8] = {0x0001, 0x7FFF, 0x8000, 0xFFFF,
                                0x0500, 0x6000, 0xF000, 0x1234};
  alignas(16) uint16_t m[8] = {0xFFFF, 0x8000, 0x0001, 0x1234,
                                0x1234, 0x5678, 0x9ABC, 0xDEF0};
  alignas(16) uint32_t out[4] = {100, 200, 300, 400};  // pre-populated accumulator
  if constexpr (is_signed && !q) {
    __asm__ __volatile__("ldr q0, [%2]\nldr q1, [%0]\nldr q2, [%1]\n.inst 0x0e625020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (is_signed && q) {
    __asm__ __volatile__("ldr q0, [%2]\nldr q1, [%0]\nldr q2, [%1]\n.inst 0x4e625020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else if constexpr (!is_signed && !q) {
    __asm__ __volatile__("ldr q0, [%2]\nldr q1, [%0]\nldr q2, [%1]\n.inst 0x2e625020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  } else {
    __asm__ __volatile__("ldr q0, [%2]\nldr q1, [%0]\nldr q2, [%1]\n.inst 0x6e625020\nstr q0, [%2]\n"
                         : : "r"(n), "r"(m), "r"(out) : "v0", "v1", "v2", "memory");
  }
  uint32_t want[4];
  uint32_t accum[4] = {100, 200, 300, 400};
  uint8_t off = q ? 4 : 0;
  for (int i = 0; i < 4; i++) {
    int64_t diff;
    if constexpr (is_signed) {
      diff = static_cast<int16_t>(n[off + i]) - static_cast<int16_t>(m[off + i]);
    } else {
      diff = static_cast<int64_t>(static_cast<uint16_t>(n[off + i])) -
             static_cast<int64_t>(static_cast<uint16_t>(m[off + i]));
    }
    want[i] = accum[i] + static_cast<uint32_t>(diff < 0 ? -diff : diff);
  }
  bool ok = (out[0] == want[0] && out[1] == want[1] &&
             out[2] == want[2] && out[3] == want[3]);
  snprintf(buf, sizeof(buf),
           "  %sABAL%s .4S out=[%08x %08x %08x %08x]: %s\n",
           is_signed ? "S" : "U", q ? "2" : " ",
           out[0], out[1], out[2], out[3], ok ? "OK" : "FAIL");
  report += buf;
  return ok;
}

}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_example_hellowidemul_MainActivity_probeWideMul(JNIEnv* env,
                                                        jobject /*this*/) {
  std::string report = "ARMv8 widening-multiply probe:\n";
  char buf[256];
  int total = 0, passed = 0;

  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Widening multiply probe:");

  auto run = [&](bool ok) {
    total++;
    if (ok) passed++;
  };

  // SMULL / UMULL (and -2 variants) for 8H, 4S, 2D.
  run(probe_mull_8h<true,  false>(report, buf));
  run(probe_mull_8h<true,  true >(report, buf));
  run(probe_mull_8h<false, false>(report, buf));
  run(probe_mull_8h<false, true >(report, buf));
  run(probe_mull_4s<true,  false>(report, buf));
  run(probe_mull_4s<true,  true >(report, buf));
  run(probe_mull_4s<false, false>(report, buf));
  run(probe_mull_4s<false, true >(report, buf));
  run(probe_mull_2d<true,  false>(report, buf));
  run(probe_mull_2d<true,  true >(report, buf));
  run(probe_mull_2d<false, false>(report, buf));
  run(probe_mull_2d<false, true >(report, buf));

  // PMULL / PMULL2 8-bit polynomial multiply (8 lanes).
  run(probe_pmull_8h<false>(report, buf));
  run(probe_pmull_8h<true >(report, buf));

  // PMULL64 / PMULL2-64 polynomial multiply (single 64-bit lane).
  run(probe_pmull_1q<false>(report, buf));
  run(probe_pmull_1q<true >(report, buf));

  // SADDL/UADDL widening add (8H form — 8b->16b).
  run(probe_addl_8h<true,  false>(report, buf));
  run(probe_addl_8h<true,  true >(report, buf));
  run(probe_addl_8h<false, false>(report, buf));
  run(probe_addl_8h<false, true >(report, buf));

  // SSUBL/USUBL widening sub (4S form — 16b->32b).
  run(probe_subl_4s<true,  false>(report, buf));
  run(probe_subl_4s<true,  true >(report, buf));
  run(probe_subl_4s<false, false>(report, buf));
  run(probe_subl_4s<false, true >(report, buf));

  // SADDL/UADDL widening add (2D form — 32b->64b).
  run(probe_addl_2d<true,  false>(report, buf));
  run(probe_addl_2d<true,  true >(report, buf));
  run(probe_addl_2d<false, false>(report, buf));
  run(probe_addl_2d<false, true >(report, buf));

  // SADDW/UADDW/SSUBW/USUBW wide add/sub (Vn already 4S, Vm narrow).
  run(probe_addsubw_4s<true,  false, false>(report, buf));  // SADDW
  run(probe_addsubw_4s<true,  true,  false>(report, buf));  // SADDW2
  run(probe_addsubw_4s<false, false, false>(report, buf));  // UADDW
  run(probe_addsubw_4s<false, true,  false>(report, buf));  // UADDW2
  run(probe_addsubw_4s<true,  false, true >(report, buf));  // SSUBW
  run(probe_addsubw_4s<true,  true,  true >(report, buf));  // SSUBW2
  run(probe_addsubw_4s<false, false, true >(report, buf));  // USUBW
  run(probe_addsubw_4s<false, true,  true >(report, buf));  // USUBW2

  // SABDL/UABDL absolute-difference widening (8H form).
  run(probe_abdl_8h<true,  false>(report, buf));
  run(probe_abdl_8h<true,  true >(report, buf));
  run(probe_abdl_8h<false, false>(report, buf));
  run(probe_abdl_8h<false, true >(report, buf));

  // SABAL/UABAL absolute-difference accumulate (4S form).
  run(probe_abal_4s<true,  false>(report, buf));
  run(probe_abal_4s<true,  true >(report, buf));
  run(probe_abal_4s<false, false>(report, buf));
  run(probe_abal_4s<false, true >(report, buf));

  snprintf(buf, sizeof(buf), "Summary: %d/%d OK\n", passed, total);
  report += buf;
  __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Summary: %d/%d OK",
                      passed, total);
  return env->NewStringUTF(report.c_str());
}
