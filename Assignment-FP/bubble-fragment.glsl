#version 330
uniform vec4 al_Color;
in vec3 vNormal;
in vec3 vViewDir;

out vec4 fragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(vViewDir);
    vec3 L = normalize(vec3(1.0, 1.0, 1.0)); // Fake light source from top-right

    // 1. Fresnel Effect: Transparent in the middle, opaque at the edges
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 2.5);

    // 2. Specular Highlight: The shiny wet reflection of the light
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(R, V), 0.0), 32.0);

    // Mix the base height-color with white edges and bright highlights
    vec3 baseColor = al_Color.rgb;
    vec3 finalColor = mix(baseColor, vec3(1.0), fresnel) + vec3(1.0) * spec;
    
    // Force the center to be completely see-through!
    float finalAlpha = max(fresnel * 0.7, spec); 
    
    fragColor = vec4(finalColor, finalAlpha);
}