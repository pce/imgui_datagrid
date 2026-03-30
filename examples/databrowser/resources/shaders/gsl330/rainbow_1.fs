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
    vec2 uv = gl_FragCoord.xy / u_resolution;
    // only x animation:
    // float hue = mod(uv.x + u_time * 0.1, 1.0);
    vec2 center = vec2(0.5, 0.5);
    vec2 toCenter = uv - center;
    float angle = atan(toCenter.y, toCenter.x);
    float hue = mod((angle + u_time * 0.3) / (2.0 * 3.1415926), 1.0);


    float radius = length(toCenter);
    float fade = smoothstep(0.8, 0.2, radius); // fade edges
    vec3 color = hsl2rgb(hue, 1.0, 0.5) * fade;


    // float saturation = 1.0;
    // float lightness = 0.5;
    // vec3 color = hsl2rgb(hue, saturation, lightness);
    FragColor = vec4(color, 1.0);
}
