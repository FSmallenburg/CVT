$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_particleData, 0);

uniform vec4 u_sfParams0; // particle texture width, height, particle count, 1/N
uniform vec4 u_sfParams1; // maxModeX, maxModeY, stepX, stepY
uniform vec4 u_sfParams2; // stepZ, logScale, suppressCentralPeak, allowOutOfPlaneModes
uniform vec4 u_sfRotation[3];
uniform vec4 u_sfBatchParams; // uvOffsetX, uvOffsetY, uvScaleX, uvScaleY

vec3 rotateScreenWaveVector(vec3 screenVector)
{
    return vec3(dot(u_sfRotation[0].xyz, screenVector),
                dot(u_sfRotation[1].xyz, screenVector),
                dot(u_sfRotation[2].xyz, screenVector));
}

vec2 particleTexCoord(int index)
{
    int textureWidth = int(u_sfParams0.x);
    int x = index - (index / textureWidth) * textureWidth;
    int y = index / textureWidth;
    return vec2((float(x) + 0.5) / u_sfParams0.x,
                (float(y) + 0.5) / u_sfParams0.y);
}

float quantizeToStep(float value, float step)
{
    if (abs(step) < 1.0e-6)
    {
        return 0.0;
    }

    float scaled = value / step;
    return sign(scaled) * floor(abs(scaled) + 0.5) * step;
}

vec3 quantizeToReciprocalLattice(vec3 waveVector)
{
    return vec3(quantizeToStep(waveVector.x, u_sfParams1.z),
                quantizeToStep(waveVector.y, u_sfParams1.w),
                quantizeToStep(waveVector.z, u_sfParams2.x));
}

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

void main()
{
    int particleCount = int(u_sfParams0.z + 0.5);
    if (particleCount <= 0)
    {
        gl_FragColor = vec4(0.02, 0.02, 0.06, 1.0);
        return;
    }

    vec2 globalUv = vec2(u_sfBatchParams.x + v_texcoord0.x * u_sfBatchParams.z,
                         u_sfBatchParams.y + v_texcoord0.y * u_sfBatchParams.w);
    float screenModeX = (globalUv.x - 0.5) * 2.0 * u_sfParams1.x;
    float screenModeY = (0.5 - globalUv.y) * 2.0 * u_sfParams1.y;
    bool suppressCentralPeak = u_sfParams2.z > 0.5;
    if (suppressCentralPeak && abs(screenModeX) < 0.5 && abs(screenModeY) < 0.5)
    {
        gl_FragColor = vec4(0.02, 0.02, 0.06, 1.0);
        return;
    }

    vec3 screenWaveVector = vec3(screenModeX * u_sfParams1.z,
                                 screenModeY * u_sfParams1.w,
                                 0.0);
    vec3 boxWaveVector = rotateScreenWaveVector(screenWaveVector);
    if (u_sfParams2.w < 0.5)
    {
        boxWaveVector.z = 0.0;
    }
    boxWaveVector = quantizeToReciprocalLattice(boxWaveVector);

    float sumReal = 0.0;
    float sumImag = 0.0;
    for (int particleIndex = 0; particleIndex < particleCount; ++particleIndex)
    {
        // Use explicit LOD so HLSL/D3D can compile this variable-length loop.
        vec4 particleData = texture2DLod(s_particleData, particleTexCoord(particleIndex), 0.0);
        float phase = dot(boxWaveVector, particleData.xyz);
        sumReal += particleData.w * cos(phase);
        sumImag += particleData.w * sin(phase);
    }

    float intensity = (sumReal * sumReal + sumImag * sumImag) * max(u_sfParams0.w, 0.0);
    float normalized = 0.0;
    if (u_sfParams2.y > 0.5)
    {
        normalized = log(1.0 + intensity) / max(log(1.0 + max(u_sfParams0.z, 1.0)), 1.0e-5);
    }
    else
    {
        normalized = intensity * max(u_sfParams0.w, 0.0);
    }

    float clampedNormalized = clamp(normalized, 0.0, 1.0);
    gl_FragColor = vec4(clampedNormalized, clampedNormalized, clampedNormalized, 1.0);
}
