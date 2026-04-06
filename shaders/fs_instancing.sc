$input v_normal, v_color

#include <bgfx_shader.sh>

uniform vec4 u_lightDir;
uniform vec4 u_lightScale;

void main()
{
    // World-space direction from the shaded point toward the light source.
    vec3 lightDir = normalize(u_lightDir.xyz);
    vec3 n = normalize(v_normal);

    float diff = max(dot(n, lightDir), 0.0);

    // Ambient + diffuse. Use bgfx splat helpers for cross-backend compatibility.
    vec3 ambient = vec3_splat(0.3);
    vec3 diffuse = vec3_splat(diff);
    float lightingScale = u_lightScale.x;

    vec3 finalColor = lightingScale * (ambient + diffuse) * v_color.rgb;

    gl_FragColor = vec4(finalColor, v_color.a);
}