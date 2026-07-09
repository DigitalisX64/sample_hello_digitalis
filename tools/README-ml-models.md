<!--
Copyright (C) 2026 utzcoz

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->

# ML sample models — install, generate, and deploy

This directory holds the host-side tooling that produces the tiny synthetic
inference models used by the machine-learning sample apps. The models are
**generated**, not downloaded, so nothing large is committed and every value is
reproducible. This document is the end-to-end workflow: install the toolchain,
generate each model, copy it into its demo, and verify it on the emulator.

## Why generate models at all

The ML samples exist to run **real inference kernels** through the ARM64→x86_64
translator (conv/dot-product/GEMM over NEON), and to assert the result against a
known-good value so a translator miscompile surfaces as a wrong number instead
of passing silently. That needs an actual model file — but a real pretrained
model is large, opaque, and its "correct" output is not hand-checkable.

Instead each generator builds a **fixed-weight** network (no training) whose
weights and input are **small integers**. Because every intermediate product and
sum stays an exact integer well under 2²⁴, the float32 arithmetic is exact —
which is also **order- and FMA-independent**, so the output is *bit-identical* on
the host (x86_64, where we compute the golden) and on the device (arm64, where
the translated kernel runs). That is what lets the sample assert an exact golden
rather than an approximate one.

## Coverage

| Sample | On-device runtime (AAR) | Generator | Model file | Golden* |
|---|---|---|---|---|
| `hello-tflite` | `org.tensorflow:tensorflow-lite:2.16.1` | `hello-tflite/tools/gen_model.py` | `assets/model.tflite` | `[27, 8, 13]` |
| `hello-onnxruntime` | `com.microsoft.onnxruntime:onnxruntime-android:1.22.0` | `hello-onnxruntime/tools/gen_model.py` | `assets/model.onnx` | `[-7, 16, -1]` |
| `hello-pytorch` | `org.pytorch:pytorch_android_lite:1.13.1` | `hello-pytorch/tools/gen_model.py` | `assets/model.ptl` | `[-7, 16, -1]` |
| `hello-litert-llm` | `com.google.mediapipe:tasks-genai:0.10.35` | — (see [LLM & speech](#llm--speech-samples-no-generated-model)) | — | — |
| `hello-vosk` | `com.alphacephei:vosk-android:0.3.47` | — (see [LLM & speech](#llm--speech-samples-no-generated-model)) | — | — |

\* The golden is what each generator **prints** when you run it; the values above
are the hand-derived expectations. Always transcribe the number the generator
actually prints into the sample (see [step 3](#3-copy-the-model-into-its-demo)).

The generator pins each framework to the **same version the sample ships**, so
the produced model is loadable by the on-device runtime. The tightest pin is
PyTorch: `pytorch_android_lite:1.13.1` loads a **lite-interpreter `.ptl`** (via
`_save_for_lite_interpreter` + `optimize_for_mobile`); a newer torch serializes a
bytecode version 1.13.1 cannot read.

---

## 1. Install the toolchain

The frameworks need CPython 3.10 (the pinned versions have no wheels for newer
Pythons such as 3.14). The bootstrap script installs it via `pyenv` and creates
three isolated, version-pinned venvs — one per framework, to avoid `numpy`/ABI
conflicts between them.

```bash
cd sample/hellodigitalis
bash tools/setup-ml-generators.sh              # venvs under ~/.venvs
# or choose a location:
VENV_ROOT=/data/venvs bash tools/setup-ml-generators.sh
```

It creates:

| Venv | Packages | Feeds |
|---|---|---|
| `digitalis-torch` | `torch==1.13.1` (CPU wheel) | `hello-pytorch` |
| `digitalis-onnx` | `onnx==1.16.2`, `onnxruntime==1.22.0`, `numpy<2` | `hello-onnxruntime` |
| `digitalis-tf` | `tensorflow-cpu==2.16.1` | `hello-tflite` |

Total ≈ 2.5 GB, one-time. The script is idempotent — re-running skips anything
already present.

**Prerequisites**
- `pyenv` installed (the script fails fast with a pointer if not).
- Network access to PyPI and `download.pytorch.org`.
- ~3 GB free disk.

**Troubleshooting**
- *`pyenv install` fails building CPython* → missing C build deps. Install the
  standard [pyenv build environment](https://github.com/pyenv/pyenv/wiki#suggested-build-environment)
  (`libssl-dev`, `zlib1g-dev`, `libbz2-dev`, `libreadline-dev`, `libsqlite3-dev`,
  `libffi-dev`, `liblzma-dev`, …), which needs sudo, then re-run.
- *A framework import fails* → delete that venv dir and re-run the script.

---

## 2. Generate the models

Run each generator with **its own venv's** Python. Each one builds the model,
runs it once to compute the golden, **writes the model straight into the sample's
`assets/` dir**, and prints the input + golden.

```bash
cd sample/hellodigitalis

~/.venvs/digitalis-torch/bin/python hello-pytorch/tools/gen_model.py
~/.venvs/digitalis-onnx/bin/python  hello-onnxruntime/tools/gen_model.py
~/.venvs/digitalis-tf/bin/python    hello-tflite/tools/gen_model.py
```

Example output (PyTorch):

```
torch    : 1.13.1
input    : [[2.0, -1.0, 3.0, 1.0]]
golden   : [-7.0, 16.0, -1.0]
wrote    : .../hello-pytorch/src/main/assets/model.ptl
```

### Model designs

All three are deliberately tiny and hand-checkable.

- **hello-pytorch** — `Linear(4→4) → ReLU → Linear(4→3)`. Input `[2,-1,3,1]`.
  `fc1` → `[2, 2, -5, 11]`, ReLU → `[2, 2, 0, 11]`, `fc2` → `[-7, 16, -1]`.
- **hello-onnxruntime** — `Gemm(4→4) → ReLU → Gemm(4→3)` (opset 13), same weights
  as the PyTorch net on purpose, so both compute `[-7, 16, -1]`.
- **hello-tflite** — `Conv2D(3×3, 1 filter, valid) → ReLU → Flatten → Dense(4→3)`.
  Input is the 4×4 grid `0..15`. Conv → `[[5,6],[9,10]]`, ReLU (unchanged),
  flatten → `[5,6,9,10]`, Dense → `[27, 8, 13]`. This exercises the conv
  dot-product path, which the trivial `out = 3*in` placeholder did not.

To change a design, edit the weight constants at the top of the generator and
re-run — the printed golden updates; transcribe the new value into the sample.

---

## 3. Copy the model into its demo

The generators already **write into the demo's `assets/`**, so no manual copy is
normally needed. If you generated elsewhere, copy the file to:

```
hello-tflite/src/main/assets/model.tflite
hello-onnxruntime/src/main/assets/model.onnx
hello-pytorch/src/main/assets/model.ptl
```

Then wire the printed golden into the sample's assertion. Each ML sample loads
its model from `assets/`, runs inference on the committed input, and compares the
output to the golden, logging the suite's markers:

- on match → `"<MODULE> OK …"`
- on mismatch → `"<MODULE> FAIL at <tag>: got=… want=…"`

The `--status` gate (`status-test-lib`) passes on `OK` and fails on the `FAIL`
marker, so the golden is what makes the sample a real translator test rather than
a smoke test. Keep the golden constants next to a comment recording the input and
the hand derivation (as the generators do).

> The generator is the source of truth for the golden. If you regenerate with
> different weights, update the sample's `want` constant to the newly printed
> value in the same change.

---

## 4. Verify on the emulator

Use the standard sample build/deploy/verify loop (see the repo's cache-trap
notes — the gradle task cache and the Berberis extract cache both silently serve
stale binaries):

```bash
cd sample/hellodigitalis
./gradlew :hello-pytorch:assembleDebug --rerun-tasks        # forces native recompile

PKG=com.example.hellodigitalis.hellopytorch
APK=hello-pytorch/build/outputs/apk/debug/hello-pytorch-debug.apk
adb uninstall $PKG; adb install -r "$APK"; adb shell pm clear $PKG   # flush both caches
```

Then run the status gate and confirm a green result:

```bash
.claude/scripts/test-samples.sh --status hello-pytorch
```

**Perturbation check (prove the golden actually gates).** Flip one golden
constant in the sample, rebuild with `--rerun-tasks`, and confirm the module now
reports `FAIL` with `got=<the real value>` — that proves the assertion bites and
that the translator computed the real value. Revert, rebuild, confirm `OK`.

If inference raises `Undefined arm64 instruction` or returns a wrong number,
that is a **translator bug** (decoder / interpreter / lite / heavy tier), not a
sample bug — record it for the dispatch loop and leave the sample as the failing
spec; do not weaken the assertion.

---

## LLM & speech samples (no generated model)

`hello-litert-llm` (`tasks-genai`) and `hello-vosk` (`vosk-android`) are **not**
covered by a generator:

- A LiteRT-LM / MediaPipe GenAI bundle is a full tokenizer + transformer package;
  a synthetic in-format bundle small enough to commit is impractical, and a real
  one is large and opaque.
- A Vosk model is a Kaldi acoustic + language model graph; likewise impractical
  to synthesize.

For these, deepen to the runtime's reachable op/tensor/decoder API with an
in-process golden check (no model file) and document the limitation in a
`NOTES.md` next to the sample, rather than shipping a large model. This keeps the
suite honest about what is actually asserted.

---

## Reproducibility notes

- **Pinned versions** (framework == on-device AAR) keep the model loadable by the
  device runtime and the golden stable across machines.
- **Integer-exact design** is why the host-computed golden matches the device
  output bit-for-bit; keep new weights/inputs small integers so accumulations
  stay exact in float32 (< 2²⁴). If a design ever needs non-integer weights,
  switch the sample to a tolerance compare and document why.
- The generators are **offline and deterministic** — no network, no RNG seeds
  that matter (weights are set explicitly), no training. Re-running yields the
  same bytes and the same golden.
