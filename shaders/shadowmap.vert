#version 450

layout (location = 0) in vec3 vPosition;
layout (location = 1) in vec3 vColor;
layout (location = 2) in vec3 vNormal;
layout (location = 3) in vec2 vUVCoord;

layout (location = 0) out VOUT
{
    vec4 position;
    vec3 lightPosition;
} vOut;

layout(push_constant) uniform PushConsts 
{
    mat4 model;
    mat4 vp; // pjocetion * view
    vec3 lightPos;
} pushConstants;

out gl_PerVertex 
{
    vec4 gl_Position;
};

void main()
{
    vOut.position = vec4(vPosition, 1.0);  
    vOut.lightPosition = pushConstants.lightPos; 

    gl_Position = pushConstants.vp * pushConstants.model * vOut.position;
}

