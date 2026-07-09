#!/usr/bin/env python
#
# Copyright (C) 2026 utzcoz
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Generates hello-tflite's synthetic fixed-weight model and prints its golden
# output. Run in the pinned TensorFlow venv (see tools/README-ml-models.md):
#
#     ~/.venvs/digitalis-tf/bin/python hello-tflite/tools/gen_model.py
#
# Model: Conv2D(3x3, 1 filter, valid) -> ReLU -> Flatten -> Dense(4->3). This
# replaces the trivial out = 3*in placeholder model and exercises the real
# NEON/dot-product conv + FC kernels. There is no *training* — the kernel and
# dense weights are fixed small integers over a fixed integer input (0..15), so
# every float32 product and accumulation is an exact integer (< 2^24). Exact
# integer float arithmetic is order- and FMA-independent, so the tflite output is
# bit-identical on host (x86_64) and device (arm64) under translation, letting
# hello-tflite assert an exact golden.
#
# The model is authored as a tf.Module + concrete function (NOT tf.keras): under
# TensorFlow 2.16 the default Keras 3 functional path trips an MLIR converter bug
# ("missing attribute 'value'") when lowering Conv2D to TFLite. The low-level
# tf.nn.* graph converts cleanly and is exactly equivalent.

import os

import numpy as np
import tensorflow as tf

# 4x4x1 input, values 0..15 (fixed, committed into the sample too).
INPUT = np.arange(16, dtype=np.float32).reshape(1, 4, 4, 1)

# 3x3 conv kernel (shape [kh, kw, in_ch, out_ch] = [3,3,1,1]).
CONV_K = np.array([[1, 0, -1],
                   [0, 1, 0],
                   [-1, 0, 1]], dtype=np.float32).reshape(3, 3, 1, 1)
CONV_B = np.array([0], dtype=np.float32)

# Dense weight (flattened-conv 4 -> 3) and bias.
DENSE_W = np.array([[1, 0, -1],
                    [2, 1, 0],
                    [0, -1, 1],
                    [1, 1, 1]], dtype=np.float32)
DENSE_B = np.array([0, 1, -1], dtype=np.float32)


class Model(tf.Module):
    def __init__(self):
        super().__init__()
        self.conv_k = tf.constant(CONV_K)
        self.conv_b = tf.constant(CONV_B)
        self.dense_w = tf.constant(DENSE_W)
        self.dense_b = tf.constant(DENSE_B)

    @tf.function(input_signature=[tf.TensorSpec([1, 4, 4, 1], tf.float32)])
    def __call__(self, x):
        # tf.nn.conv2d is cross-correlation (no kernel flip), matching Keras
        # Conv2D, so the hand-derived golden holds.
        conv = tf.nn.conv2d(x, self.conv_k, strides=[1, 1, 1, 1], padding="VALID")
        conv = tf.nn.bias_add(conv, self.conv_b)
        relu = tf.nn.relu(conv)
        flat = tf.reshape(relu, [1, 4])          # row-major -> [5, 6, 9, 10]
        return tf.matmul(flat, self.dense_w) + self.dense_b


def main():
    model = Model()
    golden = model(tf.constant(INPUT)).numpy()

    assets = os.path.normpath(
        os.path.join(os.path.dirname(__file__), "..", "src", "main", "assets"))
    os.makedirs(assets, exist_ok=True)
    out_path = os.path.join(assets, "model.tflite")

    concrete = model.__call__.get_concrete_function()
    converter = tf.lite.TFLiteConverter.from_concrete_functions([concrete], model)
    tflite_model = converter.convert()
    with open(out_path, "wb") as f:
        f.write(tflite_model)

    print("tensorflow:", tf.__version__)
    print("input     :", INPUT.flatten().astype(int).tolist(), "(4x4x1)")
    print("golden    :", golden.flatten().tolist())
    print("wrote     :", out_path, "(%d bytes)" % len(tflite_model))


if __name__ == "__main__":
    main()
