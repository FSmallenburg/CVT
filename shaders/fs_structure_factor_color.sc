$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_sfIntensity, 0);

uniform vec4 u_sfColorParams; // texelSizeX, texelSizeY, blurRadius, unused
uniform vec4 u_sfColorRange;  // minIntensity, maxIntensity, inverseRange, unused

vec3 structureFactorColor(float t)
{
    t = clamp(t, 0.0, 1.0);

    vec3 c0 = vec3(0.02, 0.02, 0.06);
    vec3 c1 = vec3(0.16, 0.05, 0.32);
    vec3 c2 = vec3(0.48, 0.12, 0.48);
    vec3 c3 = vec3(0.92, 0.52, 0.12);
    vec3 c4 = vec3(1.00, 0.97, 0.90);

    if (t < 0.25)
    {
        return mix(c0, c1, t / 0.25);
    }
    if (t < 0.50)
    {
        return mix(c1, c2, (t - 0.25) / 0.25);
    }
    if (t < 0.75)
    {
        return mix(c2, c3, (t - 0.50) / 0.25);
    }

    return mix(c3, c4, (t - 0.75) / 0.25);
}

float blurKernelWeight(int distance, int radius)
{
    if (radius <= 0)
    {
        return distance == 0 ? 1.0 : 0.0;
    }

    if (radius == 1)
    {
        if (distance == 0)
        {
            return 2.0;
        }
        if (distance == 1)
        {
            return 1.0;
        }
        return 0.0;
    }

    if (distance == 0)
    {
        return 6.0;
    }
    if (distance == 1)
    {
        return 4.0;
    }
    if (distance == 2)
    {
        return 1.0;
    }
    return 0.0;
}

float sampleBlurredIntensity(vec2 uv)
{
    int radius = int(clamp(u_sfColorParams.z, 0.0, 2.0) + 0.5);
    if (radius <= 0)
    {
        return texture2D(s_sfIntensity, uv).r;
    }

    vec2 texelSize = u_sfColorParams.xy;
    float weightedSum = 0.0;
    float totalWeight = 0.0;
    for (int dy = -2; dy <= 2; ++dy)
    {
        if (abs(dy) > radius)
        {
            continue;
        }

        float weightY = blurKernelWeight(abs(dy), radius);
        for (int dx = -2; dx <= 2; ++dx)
        {
            if (abs(dx) > radius)
            {
                continue;
            }

            float weight = weightY * blurKernelWeight(abs(dx), radius);
            vec2 offset = vec2(float(dx) * texelSize.x, float(dy) * texelSize.y);
            weightedSum += texture2D(s_sfIntensity, uv + offset).r * weight;
            totalWeight += weight;
        }
    }

    return totalWeight > 0.0 ? (weightedSum / totalWeight) : texture2D(s_sfIntensity, uv).r;
}

void main()
{
    float normalized = clamp(sampleBlurredIntensity(v_texcoord0), 0.0, 1.0);
    float minIntensity = clamp(u_sfColorRange.x, 0.0, 0.99);
    float maxIntensity = max(u_sfColorRange.y, minIntensity + 1.0e-5);
    float scaled = clamp((normalized - minIntensity) / (maxIntensity - minIntensity), 0.0, 1.0);
    vec3 color = structureFactorColor(scaled);
    gl_FragColor = vec4(color, 1.0);
}
