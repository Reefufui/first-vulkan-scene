// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout (location = 0) in  vec2  pos;
layout (location = 0) out vec2  uv;
layout (location = 1) out float faceID;

vec2 facePlacement[6] = vec2[](
    vec2(-0.75f, +0.00f),
    vec2(+0.25f, +0.00f),
    vec2(-0.25f, -2.0f / 3.0f),
    vec2(-0.25f, +2.0f / 3.0f),
    vec2(-0.25f, +0.00f),
    vec2(+0.75f, +0.00f)
);

void main() 
{
    vec2 position = pos * vec2(1.0f / 4.0f, 1.0f / 3.0f) + facePlacement[gl_InstanceIndex];

    uv = pos;
    faceID = float(gl_InstanceIndex);

    gl_Position = vec4(position, 0.0f, 1.0f);
}

