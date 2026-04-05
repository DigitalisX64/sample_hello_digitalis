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

// Shader sources extracted from shaders/es3_vertex.vert and shaders/es3_fragment.frag
// Layout locations must match POS_ATTRIB(0), COLOR_ATTRIB(1),
// SCALEROT_ATTRIB(2), OFFSET_ATTRIB(3) defined in RendererES3.cpp.

static const char ES3_VERTEX_SHADER[] = R"(#version 300 es
layout(location = 0) in vec2 pos;
layout(location = 1) in vec4 color;
layout(location = 2) in vec4 scaleRot;
layout(location = 3) in vec2 offset;
out vec4 vColor;
void main() {
    mat2 sr = mat2(scaleRot.xy, scaleRot.zw);
    gl_Position = vec4(sr*pos + offset, 0.0, 1.0);
    vColor = color;
}
)";

static const char ES3_FRAGMENT_SHADER[] = R"(#version 300 es
precision mediump float;
in vec4 vColor;
out vec4 outColor;
void main() {
    outColor = vColor;
}
)";
