// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450 core

layout (location = 0) in VOUT
{
    vec2 uv;
} vInput;

layout(location = 0) out vec4 color;

layout(set = 0, binding = 0) uniform sampler2D   texSampler;

void main()
{
    color = texture(texSampler, vInput.uv);
}
