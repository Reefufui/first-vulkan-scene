#version 450

layout(location = 0) in vec4 vertex;
layout(location = 0) out vec4 color;

void main(void)
{
    vec4 pos = vertex * vec4(0.3, 0.3, 1.0, 1.0); 

    pos   += vec4(-1.0f + float(gl_InstanceIndex) / 5,
        -1.0f + float(gl_InstanceIndex) / 5, .0f, 1.0f);
    color =  vec4(float(gl_InstanceIndex) / 10, 0.3, 0.5, 1.0f);

	// Vulkan coordinate system is different
    gl_Position = pos;
    gl_Position.y = -gl_Position.y;
}
