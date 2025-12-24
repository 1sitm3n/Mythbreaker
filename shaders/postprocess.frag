#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

// Tone mapping (ACES approximation)
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// Extract bright parts for bloom
vec3 extractBright(vec3 color, float threshold) {
    float brightness = dot(color, vec3(0.2126, 0.7152, 0.0722));
    return color * smoothstep(threshold, threshold + 0.5, brightness);
}

// Simple blur (box filter approximation)
vec3 blur(sampler2D tex, vec2 uv, vec2 texelSize) {
    vec3 result = vec3(0.0);
    float weights[9] = float[](0.0625, 0.125, 0.0625,
                                0.125,  0.25,  0.125,
                                0.0625, 0.125, 0.0625);
    int idx = 0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            vec2 offset = vec2(float(x), float(y)) * texelSize * 2.0;
            result += texture(tex, uv + offset).rgb * weights[idx++];
        }
    }
    return result;
}

void main() {
    vec2 uv = fragUV;
    vec2 texelSize = 1.0 / textureSize(sceneTex, 0);
    
    // Sample scene color
    vec3 sceneColor = texture(sceneTex, uv).rgb;
    
    // Simple bloom: extract bright + blur + add back
    vec3 bright = extractBright(sceneColor, 0.8);
    vec3 blurredBright = blur(sceneTex, uv, texelSize * 4.0);
    blurredBright = extractBright(blurredBright, 0.6);
    vec3 bloom = blurredBright * 0.6; // Stronger bloom
    
    // Combine
    vec3 color = sceneColor + bloom;
    
    // Tone mapping (HDR -> LDR)
    color = ACESFilm(color);
    
    // Vignette
    vec2 vignetteUV = uv * (1.0 - uv);
    float vignette = vignetteUV.x * vignetteUV.y * 10.0;
    vignette = pow(vignette, 0.35); // Stronger vignette
    color *= vignette;
    
    // Slight color grading - warm shadows, cool highlights
    vec3 shadows = vec3(1.05, 0.95, 0.9);
    vec3 highlights = vec3(0.95, 0.98, 1.05);
    float luminance = dot(color, vec3(0.2126, 0.7152, 0.0722));
    vec3 grade = mix(shadows, highlights, luminance);
    color *= grade;
    
    // Gamma correction (if not done elsewhere)
    color = pow(color, vec3(1.0 / 2.2));
    
    outColor = vec4(color, 1.0);
}
