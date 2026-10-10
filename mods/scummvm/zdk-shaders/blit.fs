#version 100
precision mediump float;
varying vec2 vUV;
uniform sampler2D uTex;
void main() {
    gl_FragColor = texture2D(uTex, vUV).bgra;
}
