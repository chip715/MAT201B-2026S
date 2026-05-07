#version 330
uniform mat4 al_ModelViewMatrix;
uniform mat4 al_ProjectionMatrix;
uniform mat4 al_NormalMatrix;

layout(location = 0) in vec3 position;
layout(location = 2) in vec3 normal;

out vec3 vNormal;
out vec3 vViewDir;

void main() {
    vec4 eyePos = al_ModelViewMatrix * vec4(position, 1.0);
    gl_Position = al_ProjectionMatrix * eyePos;

    // Calculate the normal and camera view direction for lighting
    vNormal = normalize(mat3(al_NormalMatrix) * normal);
    vViewDir = normalize(-eyePos.xyz);
}