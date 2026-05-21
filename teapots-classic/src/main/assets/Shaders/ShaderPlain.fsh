//
// Copyright (C) 2015 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//  ShaderPlain.fsh
//

#define USE_PHONG (1)

// region digitalis
// Originally lowp/mediump throughout; bumped to highp to remove per-pixel
// floating-point precision noise that produced a ~6% RGB diff between
// consecutive identical renders (visually identical, but enough to fail the
// 95% screenshot match threshold).
uniform highp vec3       vMaterialAmbient;
uniform highp vec4       vMaterialSpecular;

varying highp vec4 colorDiffuse;

#if USE_PHONG
uniform highp vec3      vLight0;
varying highp vec3 position;
varying highp vec3 normal;
#else
varying highp vec4 colorSpecular;
#endif

void main()
{
#if USE_PHONG
    highp vec3 halfVector = normalize(-vLight0 + position);
    highp float NdotH = max(dot(normalize(normal), halfVector), 0.0);
    highp float fPower = vMaterialSpecular.w;
    highp float specular = pow(NdotH, fPower);

    highp vec4 colorSpecular = vec4( vMaterialSpecular.xyz * specular, 1 );
    gl_FragColor = colorDiffuse + colorSpecular;
#else
    gl_FragColor = colorDiffuse + colorSpecular;
#endif
}
// endregion
