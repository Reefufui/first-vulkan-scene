#version 450 core

layout (location = 0) in VOUT
{
    vec3 normal;
    vec3 worldModel;
    vec3 worldLight;
    vec2 uv;
} vInput;

layout(location = 0) out vec4 color;

layout(set = 0, binding = 0) uniform sampler2D   texSampler;
layout(set = 1, binding = 0) uniform samplerCube shadowCubemap;

const float eps    = 0.15f;
const float shadow = 0.5f;

void main()
{
    color = texture(texSampler, vInput.uv);

    vec3 toLight = vInput.worldLight -vInput.worldModel;

    // shadowmap
    if (length(toLight) > texture(shadowCubemap, toLight).r + eps)
    {
        color.rgb *= shadow;
    }
}
