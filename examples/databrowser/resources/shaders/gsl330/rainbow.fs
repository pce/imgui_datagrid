#version 330

uniform float u_time;
uniform vec2 u_resolution;

out vec4 FragColor;

vec3 hsl2rgb(float h, float s, float l) {
    float c = (1.0 - abs(2.0 * l - 1.0)) * s;
    float x = c * (1.0 - abs(mod(h * 6.0, 2.0) - 1.0));
    float m = l - 0.5 * c;
    vec3 rgb;

    if (h < 1.0 / 6.0) rgb = vec3(c, x, 0.0);
    else if (h < 2.0 / 6.0) rgb = vec3(x, c, 0.0);
    else if (h < 3.0 / 6.0) rgb = vec3(0.0, c, x);
    else if (h < 4.0 / 6.0) rgb = vec3(0.0, x, c);
    else if (h < 5.0 / 6.0) rgb = vec3(x, 0.0, c);
    else rgb = vec3(c, 0.0, x);

    return rgb + vec3(m);
}

void main() {
    // Normalized coordinates: from -1 to +1 with aspect correction
    // vec2 uv = (gl_FragCoord.xy / u_resolution) * 2.0 - 1.0;
    vec2 uv = gl_FragCoord.xy / u_resolution;
    // uv.x *= u_resolution.x / u_resolution.y;

    // Dynamic center movement
    vec2 center = vec2(0.5, 0.5);
    center.y += 0.2 * sin(u_time * 0.5); // vertical wobble
    center.x += 0.2 * cos(u_time * 0.3); // horizontal wobble

    vec2 toCenter = uv - center;
    float angle = atan(toCenter.y, toCenter.x);
    float radius = length(toCenter * 0.5);

    // Rainbow color rotates
    float hue = mod((angle + u_time * 0.3) / (2.0 * 3.1415926), 1.0);
    float fade = smoothstep(0.7, 0.1, radius); // larger visible radius
    vec3 color = hsl2rgb(hue, 1.0, 0.5) * fade;

    FragColor = vec4(color, 1.0);
}
