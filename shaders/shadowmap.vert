#version 450

layout (location = 0) in vec3 position;

layout (location = 0) out vec4 fPosition;
layout (location = 1) out vec3 fLightPosition;

layout(push_constant) uniform PushConsts 
{
    mat4 mvp;
    vec3 lightPos;
} pushConstants;
 
out gl_PerVertex 
{
    vec4 gl_Position;
};
 
void main()
{
    fPosition = vec4(position, 1.0);  
    gl_Position = pushConstants.mvp * fPosition;
    fLightPosition = pushConstants.lightPos; 
}

