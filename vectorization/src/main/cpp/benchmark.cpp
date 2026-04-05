/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "benchmark.h"

#include <android/log.h>
#include <stdint.h>

#include <cassert>
#include <functional>

#include "auto_vectorization.h"
#include "clang_vector.h"
#include "matrix.h"
#include "omp_simd.h"

#define LOG_TAG "vectorization"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

constexpr uint32_t kNumRuns = 1'000'000;

namespace samples::vectorization {

Vec4<> result;

/**
 * Benchmarks a given matrix multiply operation.
 *
 * The multiply is given here as a callback to try to keep Clang from folding,
 * unrolling, or inlining inconsistently across each benchmarked implementation.
 * We want Clang to do as much as possible to optimize *within* the multiply
 * function itself, but inconsistent optimization of the benchmark code itself
 * could skew results.
 *
 * @param position A position vector.
 * @param translation A translation vector.
 * @param func The multiplication function to use.
 * @return The average duration per call in nanoseconds.
 */
[[nodiscard, clang::noinline]] std::chrono::nanoseconds Benchmark(
    Vec4<>& position, Mat4<>& translation,
    std::function<Vec4<>(const Vec4<>&, const Mat4<>&)> func) {
  // TODO: Move to a unit test.
  auto test = func(position, translation);
  auto expect = Vec4<>(std::array<float, 4>{20, 10, 10, 1});
  assert(test == expect);

  auto begin = std::chrono::steady_clock::now();

  // This is another attempt to prevent Clang from optimizing the benchmark
  // harness inconsistently.
#pragma clang loop unroll(disable)
  for (auto i = 0U; i < kNumRuns; i++) {
    result = func(position, translation);
  }

  auto end = std::chrono::steady_clock::now();

  return (end - begin) / kNumRuns;
}

[[nodiscard]] BenchmarkResult
BenchmarkMatrixMultiplication(Backend backend) {
  Vec4<> position(std::array<float, 4>{10.0f, 10.0f, 10.0f, 1.0f});
  Mat4<> translation{{
      {1.0f, 0.0f, 0.0f, 10.0f},
      {0.0f, 1.0f, 0.0f, 0.0f},
      {0.0f, 0.0f, 1.0f, 0.0f},
      {0.0f, 0.0f, 0.0f, 1.0f},
  }};

  switch (backend) {
    case Backend::kAutoVectorization:
      LOGI("Benchmarking auto-vectorization");
      return BenchmarkResult::success(
          Benchmark(position, translation, [](Vec4<> p, Mat4<> t) {
            return MultiplyWithAutoVectorization(t, p);
          }));
    case Backend::kCxxSimd:
#if __NDK_MAJOR__ >= 31
#error check if std::simd works yet
#endif
      LOGI("Benchmarking std::simd");
      return BenchmarkResult::failure(BenchmarkError::kNotImplemented);
    case Backend::kClangVector:
      LOGI("Benchmarking Clang vectors");
      return BenchmarkResult::success(
          Benchmark(position, translation, [](Vec4<> p, Mat4<> t) {
            return MultiplyWithClangVectors(t, p);
          }));
    case Backend::kClangMatrix:
      LOGI("Benchmarking Clang matrices");
      return BenchmarkResult::success(
          Benchmark(position, translation, [](Vec4<> p, Mat4<> t) {
            return t * p;
          }));
    case Backend::kOpenMp:
      LOGI("Benchmarking OpenMP SIMD");
      return BenchmarkResult::success(
          Benchmark(position, translation, [](Vec4<> p, Mat4<> t) {
            return MultiplyWithOpenMP(t, p);
          }));
    default:
      return BenchmarkResult::failure(BenchmarkError::kUnknownBackend);
  }
}

}  // namespace samples::vectorization
