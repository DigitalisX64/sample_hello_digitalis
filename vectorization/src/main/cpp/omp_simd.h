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

#pragma once

#include <stdint.h>

#include "matrix.h"

namespace samples::vectorization {

/**
 * Multiplies two compatible matrices and returns the result.
 */
template <typename T, size_t M, size_t N, size_t P>
Matrix<M, P, T> MultiplyWithOpenMP(const Matrix<M, N, T>& lhs,
                                   const Matrix<N, P, T>& rhs) {
  Matrix<M, P, T> result;
#pragma omp simd
  for (auto result_column_index = 0U; result_column_index < P;
       result_column_index++) {
    for (auto lhs_column_index = 0U; lhs_column_index < N; lhs_column_index++) {
      auto lhs_column = lhs.column(lhs_column_index);
      const T& scalar = rhs.get(lhs_column_index, result_column_index);
      for (auto row = 0U; row < lhs_column.size(); row++) {
        result.get(row, result_column_index) += lhs_column[row] * scalar;
      }
    }
  }
  return result;
}

}  // namespace samples::vectorization
