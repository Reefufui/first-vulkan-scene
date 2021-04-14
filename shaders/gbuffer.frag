// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450 core

layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec4 gNormal;

layout (location = 0) in VOUT
{
    vec3 position;
    vec3 normal;
    vec2 uv;
} vInput;

void main()
{
    gPosition   = vec4(vInput.position, 1.0f);
    gNormal     = vec4(normalize(vInput.normal), 1.0f);
}
