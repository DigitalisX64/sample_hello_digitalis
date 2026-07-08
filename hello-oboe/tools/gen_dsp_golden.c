#include <stdint.h>
#include <stdio.h>
#include <string.h>
enum { kFrames = 4800 };  /* mono-equivalent sample count for the checksum */
/* Deterministic FLOAT waveform, all values exact multiples of 1/128 so the
 * bit pattern is identical on any IEEE-754 target; a translator FP miscompile
 * changes the checksum. */
static float SynthF(int i) {
  return (float)(i % 100) * (1.0f/128.0f) - 0.375f;
}
static uint64_t Checksum(const float* buf, int n) {
  uint64_t h = 1469598103934665603ULL;
  for (int i = 0; i < n; ++i) {
    uint32_t bits; memcpy(&bits, &buf[i], 4);
    h ^= bits; h *= 1099511628211ULL;
  }
  return h;
}
int main(void) {
  float buf[kFrames];
  for (int i = 0; i < kFrames; ++i) buf[i] = SynthF(i);
  printf("kGolden = 0x%016llxULL\n", (unsigned long long)Checksum(buf, kFrames));
  printf("first 4: %.6f %.6f %.6f %.6f\n", buf[0], buf[1], buf[2], buf[3]);
  return 0;
}
