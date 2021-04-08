// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout (binding = 0) uniform samplerCube cubemap;

layout (location = 0) in  vec2  uv;
layout (location = 1) in  float faceID;
layout (location = 0) out vec4  color;

#define EXPOSITION 0.03f

void main()
{
    color = vec4(1.0, 1.0, 0.0f, 1.0f);

    vec3 samplePos = vec3(0.0f);

    switch (int(faceID))
    {
        // our cubemap is inverted
        case 0:
            samplePos = vec3(-1.0f, -uv.y, uv.x); // +X
            break;
        case 1:
            samplePos = vec3(+1.0f, -uv.y, -uv.x); // -X
            break;
        case 2:
            samplePos = vec3(uv.x, +1.0f, uv.y); // +Y
            break;
        case 3:
            samplePos = vec3(uv.x, -1.0f, uv.y); // -Y
            break;
        case 4:
            samplePos = vec3(uv.x, -uv.y, +1.0f); // -Z
            break;
        case 5:
            samplePos = vec3(-uv.x, -uv.y, -1.0f); // +Z
            break;
    }

    if (samplePos == vec3(0.0f))
    {
        color = vec4(0.0f);
        return;
    }
    float dist = length(texture(cubemap, samplePos).xyz) * EXPOSITION;
    color = vec4(vec3(dist), 1.0);
}
