$input a_position, a_normal, a_texcoord0
$output v_normal, v_texcoord0, v_worldPosition
#include <bgfx_shader.sh>
void main() {
    gl_Position = mul(u_modelViewProj, vec4(a_position, 1.0));
    // Inverse transpose for orthogonal TRS columns, including nonuniform
    // positive scale. Scene validation excludes zero and negative scale.
    vec3 x = mul(u_model[0], vec4(1.0, 0.0, 0.0, 0.0)).xyz;
    vec3 y = mul(u_model[0], vec4(0.0, 1.0, 0.0, 0.0)).xyz;
    vec3 z = mul(u_model[0], vec4(0.0, 0.0, 1.0, 0.0)).xyz;
    v_normal = normalize(x * a_normal.x / dot(x,x) + y * a_normal.y / dot(y,y) + z * a_normal.z / dot(z,z));
    v_texcoord0 = a_texcoord0;
    v_worldPosition = mul(u_model[0], vec4(a_position, 1.0)).xyz;
}
