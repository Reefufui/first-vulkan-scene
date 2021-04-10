// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vNormal;
layout (location = 2) in vec2 vUVCoord;

layout (location = 0) out VOUT
{
    vec3 normal;
    vec3 worldModel;
    vec3 worldLight;
    vec2 uv;
} vOut;

out gl_PerVertex
{
    vec4 gl_Position;
};

layout( push_constant ) uniform constants
{
    mat4 model;
    mat4 view;
    mat4 projection;
    vec3 lightPos;
} PushConstants;

void main() 
{
    vec4 worldPosition = PushConstants.model * vec4(vPosition, 1.0f);

    vOut.uv         = vUVCoord;
    vOut.worldLight = PushConstants.lightPos;
    vOut.worldModel = worldPosition.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(PushConstants.model)));
    vOut.normal       = normalize(normalMatrix * vNormal);

    // camera POV
    gl_Position = PushConstants.projection * PushConstants.view * worldPosition;
}

