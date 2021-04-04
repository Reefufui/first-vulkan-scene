#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vColor;
layout (location = 2) in vec3 vNormal;
layout (location = 3) in vec2 vUVCoord;

layout (location = 0) out VOUT
{
    vec3 color;
    vec3 normal;
    vec3 eyePos;
    vec3 lightVector;
    vec3 worldPos;
    vec3 lightPos;
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
    vOut.color       = vNormal;
    vOut.normal      = vNormal;
    vOut.uv          = vUVCoord;
    vOut.lightPos    = PushConstants.lightPos;
    vOut.eyePos      = vec3(PushConstants.model * vec4(vPosition, 1.0f));
    vOut.lightVector = normalize(PushConstants.lightPos - vPosition);
    vOut.worldPos    = vPosition;

    gl_Position = PushConstants.vp * PushConstants.model * vec4(vPosition, 1.0f);
}

