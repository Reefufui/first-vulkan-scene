#version 450 core

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec2 vTexCoords;

layout(location = 0) out vec4 color;

layout(binding = 1) uniform sampler2D texSampler;

void main()
{
    color = texture(texSampler, vTexCoords);
}
