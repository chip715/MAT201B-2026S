#version 400 core

layout (location = 0) in vec3 vertexPosition;
layout (location = 1) in vec4 vertexColor;
layout (location = 2) in vec2 vertexTexCoord;

out vec4 vColor;
out vec3 vWorldPos;
out vec2 uv;

uniform mat4 al_ModelViewMatrix;
uniform mat4 al_ProjectionMatrix;

void main() {
    vColor = vertexColor;
    vWorldPos = vertexPosition;
    uv = vertexTexCoord;
    gl_Position = al_ProjectionMatrix * al_ModelViewMatrix * vec4(vertexPosition, 1.0);
}