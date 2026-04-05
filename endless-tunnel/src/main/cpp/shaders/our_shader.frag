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
