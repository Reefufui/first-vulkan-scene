// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450 core

const int   ssaoKernelSize = 30;
const float ssaoRadius     = 0.2f;
const float eps            = 0.025f;

layout(set = 0, binding = 0) uniform sampler2D gPosition;
layout(set = 1, binding = 0) uniform sampler2D gNormal;
layout(set = 2, binding = 0) uniform sampler2D noiseSampler;
layout(set = 3, binding = 0) uniform ssaoKernelUBO
{
    vec4 samples[ssaoKernelSize];
} ssaoKernel;

layout (location = 0) out float color;

layout (location = 0) in VOUT
{
    vec2 uv;
    mat4 projection;
} vInput;

mat3 createTBN()
{
    vec3 N = normalize(texture(gNormal, vInput.uv).rgb);

    ivec2 frameDim = textureSize(gPosition, 0);
    ivec2 noiseDim = textureSize(noiseSampler, 0);
    // rescale uv for noice sampling
    vec2  uv = vec2(frameDim.x / noiseDim.x, frameDim.y / noiseDim.y) * vInput.uv;
    vec3 randomVec = normalize(texture(noiseSampler, uv).xyz);

    // tangent orthogonal to normal of a (normal, randomVec) plane
    vec3 tangent   = normalize(randomVec - N * dot(randomVec, N));
    vec3 bitangent = cross(tangent, N);
    return mat3(tangent, bitangent, N);
}

void main()
{
    vec3 position = texture(gPosition, vInput.uv).xyz;
    mat3 tbnMatrix = createTBN();

    float occlusion = 0.0f;
    for (int i = 0; i < ssaoKernelSize; ++i)
    {
        // tangent --> view
        vec3 samp = ssaoKernel.samples[i].xyz;
        samp = tbnMatrix * samp;
        samp = position + samp * ssaoRadius;

        // view --> clip
        vec4 offset = vec4(samp, 1.0);
        mat4 proj = vInput.projection;
        proj[1][1] *= -1;
        offset = proj * offset;
        // clip --> normalized device coords
        offset.xyz /= offset.w;
        // normalized device coords --> [0..1]
        offset.xyz = offset.xyz * 0.5f + 0.5f;

        vec3 occluderPos = texture(gPosition, offset.xy).xyz;

        float rangeCheck = smoothstep(0.0f, 1.0f, ssaoRadius / abs(position.z - occluderPos.z));

        occlusion += ((occluderPos.z >= samp.z + eps) ? 1.0f : 0.0f) * rangeCheck;
    }

    occlusion = 1.0f - (occlusion / float(ssaoKernelSize));

    color = occlusion;
}

