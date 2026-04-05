/*
 * Copyright 2013 The Android Open Source Project
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

// Shader sources extracted from shaders/es2_vertex.vert and shaders/es2_fragment.frag

static const char ES2_VERTEX_SHADER[] = R"(#version 100
uniform mat2 scaleRot;
uniform vec2 offset;
attribute vec2 pos;
attribute vec4 color;
varying vec4 vColor;
void main() {
    gl_Position = vec4(scaleRot*pos + offset, 0.0, 1.0);
    vColor = color;
}
)";

static const char ES2_FRAGMENT_SHADER[] = R"(#version 100
precision mediump float;
varying vec4 vColor;
void main() {
    gl_FragColor = vColor;
}
)";
