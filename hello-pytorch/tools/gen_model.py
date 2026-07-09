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
# Generates hello-pytorch's synthetic fixed-weight model and prints its golden
# output. Run in the pinned PyTorch venv (see tools/README-ml-models.md):
#
#     ~/.venvs/digitalis-torch/bin/python hello-pytorch/tools/gen_model.py
#
# Model: Linear(4->4) -> ReLU -> Linear(4->3). There is no *training* — the
# weights are fixed small integers, so every float32 product and sum is an exact
# integer (well under 2^24). Exact-integer float arithmetic is order- and
# FMA-independent, so the output is bit-identical on the host (x86_64) and on the
# device (arm64) under translation, letting hello-pytorch assert an exact golden.
#
# The output is serialized as a lite-interpreter model (.ptl) because the sample
# ships pytorch_android_lite:1.13.1, which loads models via LiteModuleLoader.

import os
import tempfile
import zipfile

import torch
import torch.nn as nn
from torch.utils.mobile_optimizer import optimize_for_mobile

# Fixed integer weights/biases (small so float32 stays exact).
FC1_W = [[1.0, 0.0, -1.0, 2.0],
         [0.0, 1.0, 1.0, 0.0],
         [-1.0, 2.0, 0.0, 1.0],
         [2.0, -1.0, 1.0, 0.0]]
FC1_B = [1.0, 0.0, -2.0, 3.0]
FC2_W = [[1.0, 1.0, 0.0, -1.0],
         [2.0, 0.0, 1.0, 1.0],
         [-1.0, 1.0, 1.0, 0.0]]
FC2_B = [0.0, 1.0, -1.0]

# Committed input for the golden (matches hello-onnxruntime's input on purpose).
INPUT = [[2.0, -1.0, 3.0, 1.0]]


class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.fc1 = nn.Linear(4, 4, bias=True)
        self.relu = nn.ReLU()
        self.fc2 = nn.Linear(4, 3, bias=True)

    def forward(self, x):
        return self.fc2(self.relu(self.fc1(x)))


def main():
    net = Net().eval()
    with torch.no_grad():
        net.fc1.weight.copy_(torch.tensor(FC1_W))
        net.fc1.bias.copy_(torch.tensor(FC1_B))
        net.fc2.weight.copy_(torch.tensor(FC2_W))
        net.fc2.bias.copy_(torch.tensor(FC2_B))

    inp = torch.tensor(INPUT)
    with torch.no_grad():
        out = net(inp)

    print("torch    :", torch.__version__)
    print("input    :", INPUT)
    print("golden   :", out.flatten().tolist())

    assets = os.path.normpath(
        os.path.join(os.path.dirname(__file__), "..", "src", "main", "assets"))
    os.makedirs(assets, exist_ok=True)
    out_path = os.path.join(assets, "model.ptl")

    scripted = torch.jit.script(net)
    optimized = optimize_for_mobile(scripted)

    # Save, then strip the SourceRange *.debug_pkl members. TorchScript embeds
    # them with the absolute paths of the framework source files on the build
    # machine (e.g. /home/<user>/.venvs/.../torch/nn/modules/linear.py) — build-
    # environment info that must not ship in a committed asset. The lite
    # interpreter executes bytecode.pkl and does not need the debug pickles, so
    # dropping them keeps inference identical (verified: golden unchanged).
    #
    # Save into a temp dir as "model.ptl" so the archive's internal root stays
    # the deterministic "model/" (the root is named after the file stem), keeping
    # the output byte-reproducible across runs.
    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_path = os.path.join(tmp_dir, "model.ptl")
        optimized._save_for_lite_interpreter(tmp_path)
        with zipfile.ZipFile(tmp_path) as zin, \
                zipfile.ZipFile(out_path, "w", zipfile.ZIP_STORED) as zout:
            for item in zin.infolist():
                if item.filename.endswith(".debug_pkl"):
                    continue
                zout.writestr(item, zin.read(item.filename))
    print("wrote    :", out_path)


if __name__ == "__main__":
    main()
