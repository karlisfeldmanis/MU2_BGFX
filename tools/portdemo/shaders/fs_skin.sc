$input v_normal, v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
uniform vec4 u_mode;  // x: 1 = glow (texture as light), 0 = lit
void main() {
    vec4 c = texture2D(s_color, v_texcoord0);
    if (u_mode.x > 0.5) { gl_FragColor = vec4(c.rgb, 1.0); return; }
    if (c.a < 0.3) discard;
    vec3 n = normalize(v_normal);
    float key = max(dot(n, normalize(vec3(0.5, 0.8, 0.4))), 0.0);
    float rim = max(dot(n, normalize(vec3(-0.6, 0.3, -0.7))), 0.0);
    vec3 light = vec3(0.32, 0.30, 0.38) + key * vec3(1.05, 0.95, 0.85) + rim * vec3(0.25, 0.3, 0.5);
    gl_FragColor = vec4(c.rgb * light, 1.0);
}
