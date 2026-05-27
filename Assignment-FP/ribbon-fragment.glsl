#version 400 core

in vec4 vColor;
in vec3 vWorldPos;
in vec2 uv; // uv.x = length parameter (0.0 to 1.0), uv.y = width parameter (-1.0 to 1.0)

out vec4 fragColor;

uniform float isSwarming;
uniform float bubbleModifier;

void main() {
    // Distance tracking parameters
    float widthDist = abs(uv.y);       // 0.0 at center line, 1.0 at lateral edges
    float lengthDistStart = uv.x;       // 0.0 at the absolute start of the segment
    float lengthDistEnd = 1.0 - uv.x;   // 0.0 at the absolute end of the segment

    // 1. ELIMINATE UGLY JOINTS AND CANCELLATIONS
    // To smooth out the right angles, we calculate a continuous roundness coefficient.
    // If we are near the terminal tips, we smoothly pull the edges inward.
    float jointCap = smoothstep(0.0, 0.08, lengthDistStart) * smoothstep(0.0, 0.08, lengthDistEnd);

    // 2. FRESNEL-LIKE TRANSLUCENCY PROFILE
    // Real spider silk has a soft, see-through core and highly reflective edges.
    // We shape the body opacity so the center is faint, but the density swells toward the boundaries.
    float silkBodyOpacity = mix(0.25, 0.65, smoothstep(0.0, 0.85, widthDist));

    // 3. SILK EDGE GLINT (Boosted Back-Scattering for legibility)
    // To make sure it doesn't get lost on the low-resolution sphere projection,
    // we expand the edge glint profile slightly and boost its base exposure.
    float edgeGlint = pow(smoothstep(0.70, 0.98, widthDist), 6.0);

    // 4. COLOR AND LIGHT INTENSITY MIXING
    // Inject structural white into the highlight zones while keeping your height-gradient intact
    vec3 silkColor = vColor.rgb * (1.1 + isSwarming * 0.3);
    vec3 specularHighlight = vec3(0.96, 0.98, 1.0);
    vec3 finalRGB = mix(silkColor, specularHighlight, edgeGlint * 0.7);

    // 5. COMPOSITE FINAL OPACITY CHAINS WITH ANTI-ALIASING
    // Apply the jointCap mask to round off the terminal edges completely.
    float baseAlpha = max(silkBodyOpacity, edgeGlint * 1.5);
    float finalAlpha = baseAlpha * vColor.a * jointCap;

    // Smooth feathering right at the sub-pixel geometric boundary to wipe out jagged aliasing
    float boundaryFade = smoothstep(1.0, 0.88, widthDist);
    finalAlpha *= boundaryFade;

    // Reject pixels that are completely transparent to keep the depth testing clean
    if (finalAlpha < 0.005) discard;

 
    fragColor = vec4(finalRGB, finalAlpha);
}