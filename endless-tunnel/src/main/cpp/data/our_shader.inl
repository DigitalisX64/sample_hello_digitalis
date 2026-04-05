/*
 * Copyright (C) Google Inc.
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
#ifndef _mygame_our_shader_inl
#define _mygame_our_shader_inl

// Authoritative shader source in shaders/our_shader.vert and shaders/our_shader.frag

#define OUR_VERTEX_SHADER_SOURCE R"(
uniform mat4 u_MVP;
uniform vec4 u_PointLightPos;
uniform mediump vec4 u_PointLightColor;
attribute vec4 a_Position;
attribute vec4 a_Color;
attribute vec2 a_TexCoord;
varying vec4 v_Color;
varying vec4 v_Pos;
varying float v_FogFactor;
varying vec2 v_TexCoord;
float FOG_START = 100.0;
float FOG_END = 200.0;
varying vec4 v_PointLightPos;
void main()
{
   v_Color = a_Color;
   gl_Position = u_MVP
               * a_Position;
   v_Pos = u_MVP * a_Position;
   v_PointLightPos = u_MVP * u_PointLightPos;
   v_TexCoord = a_TexCoord;
   v_FogFactor = clamp((v_Pos.z - FOG_START) / (FOG_END - FOG_START), 0.0, 1.0);
}
)"

#define OUR_FRAG_SHADER_SOURCE R"(
precision mediump float;
varying vec4 v_Color;
varying vec4 v_Pos;
varying vec2 v_TexCoord;
varying float v_FogFactor;
uniform vec4 u_Tint;
uniform sampler2D u_Sampler;
uniform vec4 u_PointLightColor;
varying vec4 v_PointLightPos;
float ATT_FACT_2 = 0.005;
float ATT_FACT_1 = 0.00;
void main()
{
   float d = distance(v_PointLightPos, v_Pos);
   float att = 1.0/(ATT_FACT_1 * d + ATT_FACT_2 * d * d);
   gl_FragColor = mix(v_Color * u_Tint * texture2D(u_Sampler, v_TexCoord) + u_PointLightColor * att, vec4(0), v_FogFactor);
}
)"

#endif
