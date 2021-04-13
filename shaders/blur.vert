// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout (location = 0) in  vec2 pos;

layout (location = 0) out VOUT
{
    vec2 uv;
} vOut;

void main() 
{
    vec2 position = pos;
    gl_Position = vec4(position, 0.0f, 1.0f);
    
    vOut.uv   = (vec2(1.0f) + pos) / 2.0f;
    vOut.uv.y = (vOut.uv.y == 1.0f) ? 0.0f : 1.0f;
}

