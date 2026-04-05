#version 100
uniform mat2 scaleRot;
uniform vec2 offset;
attribute vec2 pos;
attribute vec4 color;
varying vec4 vColor;
void main() {
    gl_Position = vec4(scaleRot*pos + offset, 0.0, 1.0);
    vColor = color;
}
