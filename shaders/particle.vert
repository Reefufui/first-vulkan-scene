// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout(location = 0) in vec4  vPosition;
layout(location = 1) in vec4  vColor;
layout(location = 2) in float vAlpha;
layout(location = 3) in float vSize;
layout(location = 4) in float vRotation;

layout (location = 0) out VOUT
{
    vec4 color;
    float alpha;
    float rotation;
} vOut;

layout( push_constant ) uniform constants
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 dummy;
} PushConstants;

void main()
{
    vOut.color    = vColor;
    vOut.alpha    = vAlpha;
    vOut.rotation = vRotation;

    gl_Position = PushConstants.projection * PushConstants.view * PushConstants.model * vPosition;
    gl_PointSize = 3.0f * vSize;
}
