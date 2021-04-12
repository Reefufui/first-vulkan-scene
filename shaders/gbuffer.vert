// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vUVCoord;

layout (location = 0) out VOUT
{
    vec3 position;
    vec3 normal;
    vec2 uv;
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
    gl_Position = PushConstants.projection * PushConstants.view * PushConstants.model * vec4(vPosition, 1.0f);

    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.view * PushConstants.model)));

    vOut.normal       = normalMatrix * vNormal;
    vOut.position     = vec3(PushConstants.view * PushConstants.model * vec4(vPosition, 1.0f));
    vOut.uv           = vUVCoord;
}
