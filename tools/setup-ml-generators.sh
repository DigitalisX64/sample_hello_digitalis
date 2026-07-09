#!/usr/bin/env bash
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
# One-shot bootstrap for the Digitalis ML-sample model generators.
#
# The default system Python may be too new for the ML frameworks (e.g. 3.14
# has no PyTorch / TensorFlow / onnxruntime wheels). This installs Python 3.10
# (via pyenv) and creates three isolated, version-pinned venvs — one per
# framework — matched to the on-device AAR versions the samples ship, so a
# generated model loads under ARM64->x86_64 translation:
#
#   torch 1.13.1  -> hello-pytorch      (.ptl lite-interpreter model)
#   onnx+ort 1.22 -> hello-onnxruntime  (.onnx Gemm->Relu->Gemm)
#   tf-cpu 2.16.1 -> hello-tflite        (.tflite conv->relu->FC)
#
# Idempotent: re-running skips anything already present. Safe to re-run.
#
# Usage:
#   bash tools/setup-ml-generators.sh                 # venvs under ~/.venvs
#   VENV_ROOT=/path bash tools/setup-ml-generators.sh # custom location
#
# See tools/README-ml-models.md for the full install/generate/verify workflow.
#
set -euo pipefail

PY_VERSION="3.10.16"
VENV_ROOT="${VENV_ROOT:-$HOME/.venvs}"
PYENV_ROOT="${PYENV_ROOT:-$HOME/.pyenv}"

log()  { printf '\n\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m  OK\033[0m %s\n' "$*"; }
err()  { printf '\033[1;31m  ERR\033[0m %s\n' "$*" >&2; }

# --- 1. Ensure pyenv + Python 3.10 --------------------------------------------
if [[ ! -x "$PYENV_ROOT/bin/pyenv" ]]; then
  err "pyenv not found at $PYENV_ROOT. Install pyenv first (https://github.com/pyenv/pyenv)."
  exit 1
fi
export PATH="$PYENV_ROOT/bin:$PATH"

PY310="$PYENV_ROOT/versions/$PY_VERSION/bin/python3.10"
if [[ ! -x "$PY310" ]]; then
  log "Installing Python $PY_VERSION via pyenv (builds from source; a few minutes)"
  # -s: skip if already installed. Needs build deps (libssl-dev, etc.); if this
  # fails, install them per https://github.com/pyenv/pyenv/wiki#suggested-build-environment
  pyenv install -s "$PY_VERSION"
fi
"$PY310" --version
ok "Python $PY_VERSION ready at $PY310"

# --- 2. Per-framework venvs ----------------------------------------------------
mkdir -p "$VENV_ROOT"

# make_venv <name> <import-check-python> <pip install args...>
make_venv() {
  local name="$1"; local check="$2"; shift 2
  # remaining "$@" is the pip install spec (packages + any options)
  local venv="$VENV_ROOT/$name"
  local py="$venv/bin/python"

  if [[ -x "$py" ]] && "$py" -c "$check" >/dev/null 2>&1; then
    ok "$name already set up ($("$py" -c "$check"))"
    return
  fi

  log "Creating venv $venv"
  "$PY310" -m venv "$venv"
  "$venv/bin/pip" install -q -U pip
  log "Installing into $name: $*"
  "$venv/bin/pip" install -q "$@"
  if "$py" -c "$check"; then
    ok "$name installed"
  else
    err "$name import check failed"
    exit 1
  fi
}

# PyTorch — CPU-only wheel (skips the ~1.8 GB CUDA build)
make_venv digitalis-torch \
  "import torch; print('torch', torch.__version__)" \
  torch==1.13.1 --index-url https://download.pytorch.org/whl/cpu

# ONNX — build graph + run for the golden
make_venv digitalis-onnx \
  "import onnx, onnxruntime as ort; print('onnx', onnx.__version__, 'ort', ort.__version__)" \
  onnx==1.16.2 onnxruntime==1.22.0 "numpy<2"

# TensorFlow — tensorflow-cpu includes the TFLite converter (~600 MB)
make_venv digitalis-tf \
  "import tensorflow as tf; print('tf', tf.__version__)" \
  tensorflow-cpu==2.16.1

# --- 3. Summary ----------------------------------------------------------------
log "All generator venvs ready under $VENV_ROOT"
printf '  %-22s %s\n' "digitalis-torch" "$VENV_ROOT/digitalis-torch/bin/python  (hello-pytorch)"
printf '  %-22s %s\n' "digitalis-onnx"  "$VENV_ROOT/digitalis-onnx/bin/python   (hello-onnxruntime)"
printf '  %-22s %s\n' "digitalis-tf"    "$VENV_ROOT/digitalis-tf/bin/python     (hello-tflite)"
echo
echo "Next: generate the models (see tools/README-ml-models.md), e.g."
echo "  ~/.venvs/digitalis-torch/bin/python hello-pytorch/tools/gen_model.py"
echo "  ~/.venvs/digitalis-onnx/bin/python  hello-onnxruntime/tools/gen_model.py"
echo "  ~/.venvs/digitalis-tf/bin/python    hello-tflite/tools/gen_model.py"
