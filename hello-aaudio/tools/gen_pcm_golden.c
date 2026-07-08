#include <stdint.h>
#include <stdio.h>
enum { kFrames = 4800 };
/* Deterministic integer waveform: a 100-sample sawtooth ramp. Exactly
 * reproducible on any target; a translator integer-arithmetic miscompile
 * changes the checksum. */
static int16_t SynthSample(int64_t frameIndex) {
  int32_t phase = (int32_t)(frameIndex % 100);   /* 0..99 */
  return (int16_t)(phase * 327 - 16350);
}
static uint64_t Checksum(const int16_t* buf, int n) {
  uint64_t h = 1469598103934665603ULL;           /* FNV-1a 64-bit */
  for (int i = 0; i < n; ++i) { h ^= (uint16_t)buf[i]; h *= 1099511628211ULL; }
  return h;
}
int main(void) {
  int16_t buf[kFrames];
  for (int i = 0; i < kFrames; ++i) buf[i] = SynthSample(i);
  printf("kGolden = 0x%016llxULL\n", (unsigned long long)Checksum(buf, kFrames));
  printf("first 4 samples: %d %d %d %d\n", buf[0], buf[1], buf[2], buf[3]);
  return 0;
}
