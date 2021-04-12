// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450 core

const int   ssaoKernelSize = 30;
const float ssaoRadius     = 0.1f;
const float eps            = 0.025f;

layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 1, binding = 0) uniform sampler2D gNormal;
layout(set = 2, binding = 0) uniform sampler2D noiseSampler;
layout(set = 3, binding = 0) uniform ssaoKernelUBO
{
    vec4 samples[ssaoKernelSize];
} ssaoKernel;

layout (location = 0) out float occlusion;

layout (location = 0) in VOUT
{
    vec2 uv;
    mat4 projection;
} vInput;

mat3 createTBN()
{
    vec3 N = normalize(texture(gNormal, vInput.uv).rgb * 2.0f - 1.0f);

    ivec2 frameDim = textureSize(gPosition, 0);
    ivec2 noiceDim = textureSize(noiseSampler, 0);
    // rescale uv for noice sampling
    vec2  uv = vec2(frameDim.x / noiceDim.x, frameDim.y / frameDim.y) * vInput.uv;
    vec3 randomVec = texture(noiseSampler, uv).xyz * 2.0f - 1.0f;

    vec3 tangent   = normalize(randomVec - N * dot(randomVec, N));
    vec3 bitangent = cross(tangent, N);
    return mat3(tangent, bitangent, N);
}

void main()
{
    vec3 position = texture(gPosition, vInput.uv).rgb;
    mat3 tbnMatrix = createTBN();

    occlusion = 0.0f;
    for (int i = 0; i < ssaoKernelSize; ++i)
    {
        vec3 samplePosition = tbnMatrix * ssaoKernel.samples[i].xyz;
        samplePosition = position + samplePosition * ssaoRadius;

        vec4 offset = vec4(samplePosition, 1.0);
        offset = vInput.projection * offset;
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5f + 0.5f;

        float sampledDepth = -texture(gPosition, offset.xy).w;

        float rangeCheck = smoothstep(0.0f, 1.0f, ssaoRadius / abs(position.z - sampledDepth));

        occlusion += ((sampledDepth >= samplePosition.z + eps) ? 1.0f : 0.0f) * rangeCheck;
    }

    occlusion = 1.0f - (occlusion / float(ssaoKernelSize));
}

