#version 450

layout (location = 0) in VOUT
{
    vec4 position;
    vec3 lightPosition;
} vInput;

layout (location = 0) out float color;

void main() 
{
    // store distance between vertex and light as light's POV image
    vec3 rayVector = vInput.position.xyz - vInput.lightPosition;
    color = length(rayVector);
}

