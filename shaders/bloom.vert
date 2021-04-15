// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vUVCoord;

layout (location = 0) out VOUT
{
    vec2 uv;
} vOut;

layout( push_constant ) uniform constants
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 lightPos;
} PushConstants;

void main() 
{
    vOut.uv = vUVCoord;
    gl_Position = PushConstants.projection * PushConstants.view * PushConstants.model * vec4(vPosition, 1.0f);
}

