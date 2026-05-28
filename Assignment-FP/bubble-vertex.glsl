#version 400 core

uniform mat4 al_ModelViewMatrix;
uniform mat4 al_ProjectionMatrix;

layout(location = 0) in vec3 position;
layout(location = 3) in vec3 normal; // RESTORED: The correct AlloLib normal slot

out vec3 vNormal;
out vec3 vViewDir;

void main() {
    vec4 eyePos = al_ModelViewMatrix * vec4(position, 1.0);
    gl_Position = al_ProjectionMatrix * eyePos;

    // Transform normals cleanly in view space
    vNormal = normalize(mat3(al_ModelViewMatrix) * normal);
    vViewDir = normalize(-eyePos.xyz);
}