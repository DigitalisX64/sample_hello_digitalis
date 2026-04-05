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
