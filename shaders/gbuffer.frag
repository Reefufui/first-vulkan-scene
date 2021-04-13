// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450 core

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;
layout (location = 2) out vec4 gAlbedoSpec;

layout (location = 0) in VOUT
{
    vec3 position;
    vec3 normal;
    vec2 uv;
} vInput;

const float near = 0.001f;
const float far  = 70.0f;

void main()
{
    gPosition   = vec4(vInput.position, 1.0f);
    gNormal     = vec4(normalize(vInput.normal), 1.0f);
    gAlbedoSpec = vec4(0.95f);
}
