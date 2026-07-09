/*
 * Copyright (C) 2026 utzcoz
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
package com.example.hellofbjni;

import com.facebook.jni.HybridData;
import com.facebook.jni.annotations.DoNotStrip;

/**
 * A minimal fbjni HybridClass: a Java object backed by a C++ peer created via
 * makeCxxInstance() in native-lib.cpp. Constructing it runs the native
 * initHybrid() (allocating the C++ DigitalisCompute with a seed and storing its
 * handle in mHybridData); combine() dispatches into the C++ peer through
 * fbjni's registered native-method thunk. This exercises fbjni's hybrid
 * machinery — the same path React Native's bridge and PyTorch Mobile use — under
 * ARM64->x86_64 translation, not merely the library load.
 *
 * The field MUST be named {@code mHybridData} of type {@code HybridData}: fbjni
 * looks it up by that exact name when wiring the native peer.
 */
public class DigitalisCompute {

    @DoNotStrip
    @SuppressWarnings("unused")
    private final HybridData mHybridData;

    private static native HybridData initHybrid(int seed);

    public DigitalisCompute(int seed) {
        mHybridData = initHybrid(seed);
    }

    /** Native compute in the C++ peer: {@code a*a - b*b + a*b + seed}. */
    public native int combine(int a, int b);
}
