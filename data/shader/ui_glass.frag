// OpenGL rendering of the layers in yzrt / pure CSS liquid glass / .box.
#version 130
uniform sampler2D gScene;
uniform vec2 gViewport;
uniform vec2 gOrigin;
uniform vec4 gPanel;
uniform vec4 gTint;
uniform vec3 gGlassColor;
uniform vec4 gCorners;
uniform float gRadius;
uniform float gOpacity;
out vec4 FragColor;

float sdBox(vec2 p, vec4 box, float radius)
{
    vec2 center = p - box.xy - box.zw * 0.5;
    float enabled = center.y < 0.0 ? (center.x < 0.0 ? gCorners.x : gCorners.y) : (center.x < 0.0 ? gCorners.z : gCorners.w);
    float r = min(radius, min(box.z, box.w) * 0.5) * enabled;
    vec2 q = abs(center) - box.zw * 0.5 + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}
float sdPanel(vec2 p) { return sdBox(p, gPanel, gRadius); }
float gaussian(float distance, float sigma)
{
    return exp(-0.5 * distance * distance / (sigma * sigma));
}
// An inset CSS shadow exposes the inside of the shifted, contracted contour.
float insetHighlight(vec2 p, vec2 offset, float spread)
{
    return smoothstep(-1.0, 1.0, sdPanel(p - offset) + spread);
}
float diagonalHighlight(vec2 p)
{
    vec2 local = p - gPanel.xy;
    float t = (local.x + gPanel.w - local.y) / (gPanel.z + gPanel.w);
    return max(1.0 - clamp(t / 0.25, 0.0, 1.0), clamp((t - 0.75) / 0.25, 0.0, 1.0));
}
void main()
{
    vec2 p = vec2(gl_FragCoord.x - gOrigin.x, gViewport.y - (gl_FragCoord.y - gOrigin.y));
    float d = sdPanel(p);
    float coverage = 1.0 - smoothstep(-0.7, 0.7, d);
    float chroma = max(max(gTint.r, gTint.g), gTint.b) - min(min(gTint.r, gTint.g), gTint.b);
    float colored = smoothstep(0.04, 0.20, chroma);
    // The original 25% UI fill is the baseline, rather than capping glass at
    // 50% even with a 100% slider. Still respect animated fade-in/out alpha.
    // Colored surfaces remain denser at intermediate slider positions.
    float opacity = mix(gOpacity, sqrt(gOpacity), colored);
    float strength = clamp(gTint.a * 4.0, 0.0, 1.0) * opacity;
    // CSS: 0 4px 8px rgba(0,0,0,.2). Sigma = blur-radius / 2.
    float shadow = 0.2 * gaussian(max(sdPanel(p - vec2(0.0, 4.0)), 0.0), 4.0);
    if(d > 0.7)
    {
        FragColor = vec4(0.0, 0.0, 0.0, shadow * strength);
        return;
    }
    // CSS backdrop-filter: blur(2px). Separable 3x3 Gaussian taps.
    vec2 uv = vec2(p.x, gViewport.y - p.y) / gViewport;
    vec2 tap = vec2(2.0) / gViewport;
    vec3 scene = texture(gScene, uv).rgb * 0.25;
    scene += texture(gScene, uv + vec2(tap.x, 0.0)).rgb * 0.125;
    scene += texture(gScene, uv - vec2(tap.x, 0.0)).rgb * 0.125;
    scene += texture(gScene, uv + vec2(0.0, tap.y)).rgb * 0.125;
    scene += texture(gScene, uv - vec2(0.0, tap.y)).rgb * 0.125;
    scene += texture(gScene, uv + tap).rgb * 0.0625;
    scene += texture(gScene, uv - tap).rgb * 0.0625;
    scene += texture(gScene, uv + vec2(tap.x, -tap.y)).rgb * 0.0625;
    scene += texture(gScene, uv + vec2(-tap.x, tap.y)).rgb * 0.0625;
    vec3 color = mix(scene, vec3(1.0), 0.04);
    // Preserve UI contrast and original colors: neutral glass uses a 48%
    // material fill, colored controls use 90% instead of washing into the scene.
    color = mix(color, gTint.rgb, mix(0.48, 0.90, colored));
    // White is neutral; saturated tints keep selected control colors legible.
    float tintAmount = max(max(gGlassColor.r, gGlassColor.g), gGlassColor.b) - min(min(gGlassColor.r, gGlassColor.g), gGlassColor.b);
    tintAmount = max(tintAmount, 1.0 - max(max(gGlassColor.r, gGlassColor.g), gGlassColor.b));
    color = mix(color, gGlassColor, tintAmount * mix(0.42, 0.18, colored));
    // CSS inset 0 0 2px black .8, and the dark double border.
    color *= 1.0 - 0.8 * gaussian(max(-d, 0.0), 1.0);
    color = mix(color, vec3(0.2), 0.08 * gaussian(d + 0.5, 0.35));
    // Opposing 2px/-2px and 6px/-6px highlights; no full white outline.
    float fine = max(insetHighlight(p, vec2(2.0, -2.0), -1.0), insetHighlight(p, vec2(-2.0, 2.0), -1.0));
    float wide = max(insetHighlight(p, vec2(6.0, -6.0), -6.0), insetHighlight(p, vec2(-6.0, 6.0), -6.0));
    // User-tuned subtle rim: highlights stay within roughly 1px of the edge.
    float edgeLight = gaussian(max(-d - 0.3, 0.0), 0.65);
    color = mix(color, vec3(1.0), wide * edgeLight * 0.10);
    color = mix(color, vec3(1.0), fine * edgeLight * 0.24);
    // :before: black 1px contour inset 8px, top:35%, blur(8px).
    if(min(gPanel.z, gPanel.w) > 18.0)
    {
        vec4 inner = vec4(gPanel.x + 8.0, gPanel.y + gPanel.w * 0.35, gPanel.z - 16.0, gPanel.w - 16.0);
        float innerD = sdBox(p, inner, max(gRadius - 8.0, 0.0));
        color *= 1.0 - 0.9 * 0.1 * gaussian(innerD, 4.0);
    }
    // :after: 45deg gradient, white .8 at opposing corners, clear 25%-75%.
    // Use pixel axes to preserve the angle on wide rectangular controls.
    // Gaussian blur(3px) along the gradient normal.
    vec2 gradientNormal = normalize(vec2(1.0, -1.0));
    float diagonal = diagonalHighlight(p) * 0.4;
    diagonal += (diagonalHighlight(p + gradientNormal * 1.5) + diagonalHighlight(p - gradientNormal * 1.5)) * 0.24;
    diagonal += (diagonalHighlight(p + gradientNormal * 3.0) + diagonalHighlight(p - gradientNormal * 3.0)) * 0.06;
    color = mix(color, vec3(1.0), diagonal * edgeLight * 0.12);
    // circle-overlay: inset 4.5px white .2, blur(1px).
    color = mix(color, vec3(1.0), gaussian(d + 0.8, 0.5) * 0.025);
    color *= 0.9;
    FragColor = vec4(color, coverage * strength);
}
