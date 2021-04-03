#version 450

layout (location = 0) in vec4 vPosition;
layout (location = 1) in vec3 vLightPosition;

layout (location = 0) out float color;

void main() 
{
    vec3 rayVector = vPosition.xyz - vLightPosition;
    color = length(rayVector);
}

