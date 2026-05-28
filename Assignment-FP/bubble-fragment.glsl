#version 400 core

// Keep the uniform declared so your C++ code doesn't throw a "missing uniform" error
uniform vec4 al_Color;

in vec3 vNormal;
in vec3 vViewDir;

out vec4 fragColor;

void main() {
    // Drop every pixel out of the rendering pipeline instantly.
    // The force field physics and C++ loops stay 100% active, 
    // but the geometry becomes completely invisible.
    discard;
}