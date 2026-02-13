#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D sceneTex;

layout(push_constant) uniform PostProcessParams {
    float bloomIntensity;
    float bloomThreshold;
    float vignetteStrength;
    float filmGrainAmount;
    float exposureAdjust;
    float saturation;
    float time;
    float padding;
} params;

vec3 ACESFilm(vec3 x) {
    float a = 2.51; float b = 0.03;
    float c = 2.43; float d = 0.59; float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

vec3 bloomSample(vec2 uv, vec2 texelSize) {
    vec3 result = vec3(0.0);
    result += texture(sceneTex, uv).rgb * 0.25;
    result += texture(sceneTex, uv + texelSize * vec2( 1.0,  0.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2(-1.0,  0.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2( 0.0,  1.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2( 0.0, -1.0)).rgb * 0.125;
    result += texture(sceneTex, uv + texelSize * vec2( 2.0,  2.0)).rgb * 0.0625;
    result += texture(sceneTex, uv + texelSize * vec2(-2.0,  2.0)).rgb * 0.0625;
    result += texture(sceneTex, uv + texelSize * vec2( 2.0, -2.0)).rgb * 0.0625;
    result += texture(sceneTex, uv + texelSize * vec2(-2.0, -2.0)).rgb * 0.0625;
    return result;
}

vec3 extractBright(vec3 color, float threshold) {
    float brightness = luminance(color);
    float soft = brightness - threshold + 0.5;
    soft = clamp(soft, 0.0, 1.0);
    soft = soft * soft;
    float contribution = max(soft, brightness - threshold);
    contribution = max(contribution, 0.0) / max(brightness, 0.0001);
    return color * contribution;
}

vec3 computeBloom(vec2 uv, vec2 texelSize) {
    vec3 bloom = vec3(0.0);
    bloom += extractBright(bloomSample(uv, texelSize * 2.0), params.bloomThreshold) * 0.5;
    bloom += extractBright(bloomSample(uv, texelSize * 4.0), params.bloomThreshold * 0.8) * 0.3;
    bloom += extractBright(bloomSample(uv, texelSize * 8.0), params.bloomThreshold * 0.6) * 0.2;
    return bloom;
}

float hash(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec2 uv = fragUV;
    vec2 texelSize = 1.0 / textureSize(sceneTex, 0);

    vec3 sceneColor = texture(sceneTex, uv).rgb;
    sceneColor *= exp2(params.exposureAdjust);

    vec3 bloom = computeBloom(uv, texelSize);
    vec3 color = sceneColor + bloom * params.bloomIntensity;

    color = ACESFilm(color);

    // Color grading
    float lum = luminance(color);
    vec3 shadowTint = vec3(1.04, 0.96, 0.92);
    vec3 highlightTint = vec3(0.96, 0.98, 1.04);
    color *= mix(shadowTint, highlightTint, smoothstep(0.0, 1.0, lum));
    vec3 gray = vec3(luminance(color));
    color = mix(gray, color, params.saturation);

    // Vignette
    vec2 d = uv - 0.5;
    color *= 1.0 - dot(d, d) * params.vignetteStrength * 2.0;

    // Film grain
    float grain = (hash(uv * 1000.0 + fract(params.time) * 100.0) * 2.0 - 1.0) * params.filmGrainAmount;
    color += grain;

    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
    outColor = vec4(color, 1.0);
}
