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
    mat4 vp; // projection * view
    vec3 lightPos;
} PushConstants;

void main() 
{
    vec4 worldPosition = PushConstants.model * vec4(vPosition, 1.0f);

    vOut.uv         = vUVCoord;
    vOut.worldLight = PushConstants.lightPos;
    vOut.worldModel = worldPosition.xyz;
    vOut.normal     = vNormal;

    // camera POV
    gl_Position = PushConstants.vp * worldPosition;
}

