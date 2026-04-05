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

#include <array>
#include <cstdint>
#include <iostream>
#include <ostream>

namespace samples::vectorization {

/// Lightweight non-owning view of contiguous T elements (replaces std::span).
template <typename T>
struct Span {
  T* ptr;
  size_t len;
  T* data() const { return ptr; }
  size_t size() const { return len; }
  T& operator[](size_t i) const { return ptr[i]; }
};

template <size_t Rows, size_t Columns, typename T = float>
class Matrix {
 public:
  Matrix() = default;
  Matrix(T (&&cells)[Rows][Columns]) {
    for (size_t row = 0; row < Rows; row++) {
      for (size_t column = 0; column < Columns; column++) {
        get(row, column) = cells[row][column];
      }
    }
  }

  // Convenience constructor for vectors (only valid when Columns == 1).
  Matrix(const std::array<T, Rows> cells) : cells_(cells) {
    static_assert(Columns == 1, "Array constructor only for column vectors");
  }

  constexpr const T* data() const { return cells_.data(); }
  constexpr T* data() { return cells_.data(); }

  constexpr T& get(size_t row, size_t column) {
    return cells_[column * Rows + row];
  }

  constexpr const T& get(size_t row, size_t column) const {
    return cells_[column * Rows + row];
  }

  Span<const T> column(size_t col) const {
    return {&get(0, col), Rows};
  }

  Span<T> column(size_t col) {
    return {&get(0, col), Rows};
  }

  bool operator==(const Matrix<Rows, Columns, T>& rhs) const {
    return cells_ == rhs.cells_;
  }

  friend std::ostream& operator<<(std::ostream& stream,
                                  const Matrix<Rows, Columns, T>& m) {
    stream << "{" << std::endl;
    for (size_t row = 0; row < Rows; row++) {
      stream << "\t{";
      for (size_t column = 0; column < Columns; column++) {
        stream << m.get(row, column);
        if (column != Columns - 1) {
          stream << ", ";
        }
      }
      stream << "}" << std::endl;
    }
    stream << "}";
    return stream;
  }

  /**
   * Multiplies two compatible matrices and returns the result.
   */
  template <size_t OtherRows, size_t OtherColumns>
  Matrix<Rows, OtherColumns, T> operator*(
      const Matrix<OtherRows, OtherColumns, T>& rhs) const {
    static_assert(OtherRows == Columns, "Incompatible matrix dimensions");
    Matrix<Rows, OtherColumns, T> result;
    for (size_t i = 0; i < Rows; i++) {
      for (size_t j = 0; j < OtherColumns; j++) {
        T sum = {};
        for (size_t k = 0; k < Columns; k++) {
          sum += get(i, k) * rhs.get(k, j);
        }
        result.get(i, j) = sum;
      }
    }
    return result;
  }

 private:
  std::array<T, Rows * Columns> cells_ = {};
};

// Enables automatic deduction of definitions like `Matrix m{{1, 0}, {0, 1}}`
// without needing to specify `Matrix<2, 2, int>`.
template <size_t Rows, size_t Columns, typename T>
Matrix(T (&&)[Rows][Columns]) -> Matrix<Rows, Columns, T>;

template <typename T = float>
using Mat4 = Matrix<4, 4, T>;

template <typename T = float>
using Vec4 = Matrix<4, 1, T>;

}  // namespace samples::vectorization
