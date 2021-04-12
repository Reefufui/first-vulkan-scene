// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout (location = 0) in VOUT
{
    vec2 uv;
} vInput;

layout (location = 0) out float color;

const int window = 2;

void main() 
{
    vec2 texelSize = 1.0f / vec2(textureSize(texSampler, 0));

    color = 0.0f;
    float num = 0.0f;

    for (int x = -window; x < window; ++x)
    {
        for (int y = -window; y < window; ++y)
        {
            vec2 xy = vec2(float(x), float(y)) * texelSize;
            color += texture(texSampler, vInput.uv + xy).r;
            num += 1.0f;
        }
    }

    color /= num;
}

