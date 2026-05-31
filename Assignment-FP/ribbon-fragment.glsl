#version 400 core

in vec4 vColor;
in vec3 vWorldPos;
in vec2 uv; // uv.x = length (0.0 to 1.0), uv.y = width (-1.0 to 1.0)

out vec4 fragColor;

uniform float lightIntensity;

void main() {
    float edgeDist = abs(uv.y);

    // 1. COLLECT BASE DISTANCE COLOR FROM C++
    vec3 baseColor = vColor.rgb * lightIntensity;

    // 2. HIGH-CONTRAST FOCUS SHARPENING MATH
    // Instead of letting the blur scale with geometric thickness, we force the core 
    // to remain punchy and sharp. We compress the falloff using high-power scaling thresholds.
    float silkCore = smoothstep(0.4, 0.0, edgeDist); 
    float softSheen = smoothstep(1.0, 0.7, edgeDist); // Keeps the ribbon fully solid across 70% of its width

    // 3. CONVERGENCE TAPER (The Hotspot Dimmer)
    float hubDampener = smoothstep(0.0, 0.15, uv.x) * smoothstep(0.0, 0.15, 1.0 - uv.x);

    // 4. COLOR MODULATION WITH EXPOSURE CONTROL
    // Brighten the main color map to ensure clarity on the multi-projector Allosphere screen.
    vec3 coreHighlight = baseColor * 2.0; 
    vec3 animatedRGB = mix(baseColor * softSheen, coreHighlight, silkCore);
    
    // Crisp, bright glint right at the absolute center line
    animatedRGB += vec3(0.9, 0.95, 1.0) * silkCore * 0.4;
    
    // Apply the hub dampener directly to the RGB intensity to prevent white clipping
    animatedRGB *= mix(0.4, 1.0, hubDampener);

    // 5. JOINT TERMINATIONS AND ALPHA PASS
    float jointCap = smoothstep(0.0, 0.05, uv.x) * smoothstep(0.0, 0.05, 1.0 - uv.x);

    // Force high alpha opacity across the ribbon body so wide lines remain crisp and visible
    float finalAlpha = mix(softSheen * 0.8, 1.0, silkCore) * vColor.a * jointCap * mix(0.6, 1.0, hubDampener);

    // 6. SUB-PIXEL ANTIALIASING FEATHERING
    // Strictly isolate edge feathering to the outermost 5% of the ribbon boundary, 
    // guaranteeing clean rendering without re-introducing focus blur.
    finalAlpha *= smoothstep(1.0, 0.95, edgeDist);

    if (finalAlpha < 0.005) discard;

    fragColor = vec4(animatedRGB, finalAlpha);
}