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

    // 2. CYLINDRICAL CORE MATH (Slightly narrowed to keep lines elegant)
    float silkCore = exp(-pow(edgeDist * 3.0, 2.0));
    float softSheen = pow(1.0 - edgeDist, 1.5);

    // 3. CONVERGENCE TAPER (The Hotspot Dimmer)
    // Generates a smooth falloff factor that dips near the terminal endpoints (0.0 and 1.0).
    // This allows the ribbon to be vibrant and fully opaque in open air spaces, 
    // but dims it smoothly right as it enters the crowded convergence hubs.
    float hubDampener = smoothstep(0.0, 0.15, uv.x) * smoothstep(0.0, 0.15, 1.0 - uv.x);

    // 4. COLOR MODULATION WITH EXPOSURE CONTROL
    // We heavily dim the white glare accent down to a tiny 0.05 scaling factor,
    // and route the main brightness through the dampener to limit additive overdrive.
    vec3 coreHighlight = baseColor * 1.2; 
    vec3 animatedRGB = mix(baseColor * softSheen, coreHighlight, silkCore);
    
    // Tiny, heavily restrained glint at the absolute center
    animatedRGB += vec3(0.8, 0.9, 1.0) * silkCore * 0.05;
    
    // Apply the hub dampener directly to the RGB intensity to prevent white clipping
    animatedRGB *= mix(0.4, 1.0, hubDampener);

    // 5. JOINT TERMINATIONS AND ALPHA PASS
    float jointCap = smoothstep(0.0, 0.05, uv.x) * smoothstep(0.0, 0.05, 1.0 - uv.x);

    // Scale the alpha with the hub dampener to thin out the geometry at the cluster center
    float finalAlpha = (softSheen * 0.4 + silkCore * 0.6) * vColor.a * jointCap * mix(0.5, 1.0, hubDampener);
    finalAlpha = max(finalAlpha, silkCore * 0.15 * jointCap * hubDampener);

    // Sub-pixel edge anti-aliasing feathering step
    finalAlpha *= smoothstep(1.0, 0.85, edgeDist);

    if (finalAlpha < 0.005) discard;

    fragColor = vec4(animatedRGB, finalAlpha);
}