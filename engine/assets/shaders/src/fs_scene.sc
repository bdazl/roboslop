$input v_normal, v_texcoord0, v_worldPosition
#include <bgfx_shader.sh>
SAMPLER2D(s_albedo, 0);
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_pointLightPosition;
uniform vec4 u_pointLightColor;
void main() {
    vec4 albedo = texture2D(s_albedo, v_texcoord0);
    vec3 normal = normalize(v_normal);
    float directionalDiffuse = max(dot(normal, -u_lightDir.xyz), 0.0);

    vec3 toPointLight = u_pointLightPosition.xyz - v_worldPosition;
    float pointDistance = length(toPointLight);
    vec3 pointDirection = toPointLight / max(pointDistance, 0.0001);
    float rangeFraction = max(1.0 - pointDistance / max(u_pointLightPosition.w, 0.0001), 0.0);
    float pointAttenuation = rangeFraction * rangeFraction;
    float pointDiffuse = max(dot(normal, pointDirection), 0.0) * pointAttenuation;

    vec3 lighting = vec3(0.15, 0.15, 0.15);
    lighting += u_lightColor.rgb * directionalDiffuse * u_lightDir.w;
    lighting += u_pointLightColor.rgb * pointDiffuse * u_pointLightColor.w;
    gl_FragColor = vec4(albedo.rgb * lighting, albedo.a);
}
