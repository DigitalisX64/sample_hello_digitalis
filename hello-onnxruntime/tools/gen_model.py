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
# Generates hello-onnxruntime's synthetic fixed-weight model and prints its
# golden output. Run in the pinned ONNX venv (see tools/README-ml-models.md):
#
#     ~/.venvs/digitalis-onnx/bin/python hello-onnxruntime/tools/gen_model.py
#
# Model: Gemm(4->4) -> ReLU -> Gemm(4->3) (opset 13), the ONNX equivalent of two
# fully-connected layers. There is no *training* — the initializers are fixed
# small integers, so every float32 product/sum is an exact integer (< 2^24) and
# the output is bit-identical on host (x86_64) and device (arm64) under
# translation. The weights match hello-pytorch, so both frameworks compute the
# same golden [-7, 16, -1] — a nice cross-framework sanity check.

import os

import numpy as np
import onnx
import onnxruntime as ort
from onnx import TensorProto, helper, numpy_helper

W1 = np.array([[1, 0, -1, 2],
               [0, 1, 1, 0],
               [-1, 2, 0, 1],
               [2, -1, 1, 0]], dtype=np.float32)
B1 = np.array([1, 0, -2, 3], dtype=np.float32)
W2 = np.array([[1, 1, 0, -1],
               [2, 0, 1, 1],
               [-1, 1, 1, 0]], dtype=np.float32)
B2 = np.array([0, 1, -1], dtype=np.float32)

INPUT = np.array([[2, -1, 3, 1]], dtype=np.float32)


def build_model():
    x = helper.make_tensor_value_info("input", TensorProto.FLOAT, [1, 4])
    y = helper.make_tensor_value_info("output", TensorProto.FLOAT, [1, 3])
    initializers = [
        numpy_helper.from_array(W1, "W1"),
        numpy_helper.from_array(B1, "B1"),
        numpy_helper.from_array(W2, "W2"),
        numpy_helper.from_array(B2, "B2"),
    ]
    # Gemm(input, W, B) with transB=1 computes input @ W^T + B, i.e. a Linear
    # layer whose weight rows are the output units.
    n1 = helper.make_node("Gemm", ["input", "W1", "B1"], ["h"], transB=1)
    n2 = helper.make_node("Relu", ["h"], ["r"])
    n3 = helper.make_node("Gemm", ["r", "W2", "B2"], ["output"], transB=1)
    graph = helper.make_graph([n1, n2, n3], "digitalis_onnx", [x], [y], initializers)
    # opset 13 / IR 8 is comfortably within onnxruntime-android 1.22's support.
    model = helper.make_model(
        graph, opset_imports=[helper.make_opsetid("", 13)], ir_version=8)
    onnx.checker.check_model(model)
    return model


def main():
    model = build_model()
    assets = os.path.normpath(
        os.path.join(os.path.dirname(__file__), "..", "src", "main", "assets"))
    os.makedirs(assets, exist_ok=True)
    out_path = os.path.join(assets, "model.onnx")
    onnx.save(model, out_path)

    sess = ort.InferenceSession(out_path, providers=["CPUExecutionProvider"])
    out = sess.run(None, {"input": INPUT})[0]

    print("onnxruntime:", ort.__version__)
    print("input      :", INPUT.tolist())
    print("golden     :", out.flatten().tolist())
    print("wrote      :", out_path)


if __name__ == "__main__":
    main()
