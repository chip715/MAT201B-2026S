#version 400
uniform vec4 al_Color;
in vec3 vNormal;
in vec3 vViewDir;

out vec4 fragColor;

// Procedural rainbow generator
vec3 iridescence(float t) {
    vec3 a = vec3(0.5, 0.5, 0.5);
    vec3 b = vec3(0.5, 0.5, 0.5);
    vec3 c = vec3(1.0, 1.0, 1.0);
    vec3 d = vec3(0.00, 0.33, 0.67);
    return a + b * cos(6.28318 * (c * t + d));
}

void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(vViewDir);
    vec3 L = normalize(vec3(1.0, 1.0, 1.0)); 

    float ndotv = abs(dot(N, V));
    
    // 1. Rainbow Oil Slick (Multiply by 3.0 to get more color bands)
    vec3 rainbow = iridescence(ndotv * 3.0); 

    // 2. Fresnel (Sharper transparency falloff at the edges)
    float fresnel = pow(1.0 - ndotv, 3.0);
    
    // 3. Specular Dot (Increased to 128.0 for a tiny, sharp, wet glint)
    vec3 R = reflect(-L, N);
    float spec = pow(max(dot(R, V), 0.0), 128.0); 

    // 4. Color Mixing (No more white wash-out!)
    // Smoothly blend the particle's height color with the rainbow edge
    vec3 finalColor = mix(al_Color.rgb, rainbow, fresnel);
    
    // Add the bright white specular glint exclusively on top
    finalColor += vec3(1.0) * spec;
    
    // 5. Alpha (Highly transparent center, opaque edges + highlight)
    float finalAlpha = max(fresnel * 0.9, spec); 
    finalAlpha = max(finalAlpha, 0.1); 
    
    fragColor = vec4(finalColor, finalAlpha);
}