// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vUVCoord;

layout (location = 0) out VOUT
{
    vec4 position;
    vec3 lightPosition;
} vOut;

layout(push_constant) uniform PushConsts 
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 lightPos;
} pushConstants;

void main()
{
    // positions in world coordinates
    vOut.position = pushConstants.model * vec4(vPosition, 1.0f);
    vOut.lightPosition = pushConstants.lightPos; 

    // camera POV
    gl_Position = pushConstants.projection * pushConstants.view * vOut.position;
}

