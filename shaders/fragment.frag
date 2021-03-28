#version 450 core

layout(location = 0) in vec4 vcolor;
layout(location = 0) out vec4 color;

void main()
{
    color = vec4(1.0, 0.5, 0.0f, 1.0f);
    color = vcolor;
}
