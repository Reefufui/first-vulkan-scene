#version 330

//layout(location = 0) in vec2 vertex;

void main(void)
{
  //gl_Position = vec4(vertex,0.0,1.0);
  if(gl_VertexIndex == 0)
    gl_Position = vec4(-0.5, -0.5, 0.0, 1.0);
  else if(gl_VertexIndex == 1) 
    gl_Position = vec4(0.5f, -0.5f, 0.0, 1.0);
  else 
    gl_Position = vec4(0.0f, +0.5f, 0.0, 1.0);

  gl_Position.y = -gl_Position.y;	// Vulkan coordinate system is different t OpenGL    
}
