#version 400 core

in vec4 vColor;
in vec3 vWorldPos;
in vec2 uv; 

out vec4 fragColor;
uniform float lightIntensity;

void main() {
    float edgeDist = abs(uv.y);
    float lengthEdge = min(uv.x, 1.0 - uv.x);

    // 1. HARDWARE-FAST LINEAR RAMPS (Replaces costly smoothsteps)
    float jointCap = clamp(lengthEdge * 20.0, 0.0, 1.0);       
    
    // FIX: Proper mathematical mix to avoid flat visual dead-zones at the joints
    float linearDampener = clamp(lengthEdge * 6.666, 0.0, 1.0);
    float hubDampener = 0.4 + (0.6 * linearDampener);  

    float silkCore = clamp((0.4 - edgeDist) * 2.5, 0.0, 1.0); 
    float softSheen = clamp((1.0 - edgeDist) * 3.333, 0.0, 1.0); 
    float edgeFeather = clamp((1.0 - edgeDist) * 20.0, 0.0, 1.0); 

    // 2. EXPOSURE & COLOR
    vec3 baseColor = vColor.rgb * lightIntensity;
    
    // Core highlight + pre-baked central glint factor
    vec3 coreHighlight = baseColor * 2.0 + vec3(0.36, 0.38, 0.4); 
    
    vec3 animatedRGB = mix(baseColor * softSheen, coreHighlight, silkCore) * hubDampener;
    
    float finalAlpha = mix(softSheen * 0.8, 1.0, silkCore) * vColor.a * jointCap * hubDampener * edgeFeather;

    // Discard skip
    if (finalAlpha < 0.005) discard;

    fragColor = vec4(animatedRGB, finalAlpha);
}