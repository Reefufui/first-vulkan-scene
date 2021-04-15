// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout (location = 0) in VOUT
{
    vec2 uv;
} vInput;

layout (location = 0) out vec4 color;

// http://dev.theomader.com/gaussian-kernel-calculator/
// sigma = 1.7, kernel size = 11
float kernel[6 * 6] = float[](
        0.053645,   0.045345,   0.027385,   0.011814,   0.00364,    0.0008,
        0.045345,   0.038329,   0.023148,   0.009986,   0.003077,   0.000677,
        0.027385,   0.023148,   0.01398,    0.006031,   0.001858,   0.000409,
        0.011814,   0.009986,   0.006031,   0.002602,   0.000802,   0.000176,
        0.00364,    0.003077,   0.001858,   0.000802,   0.000247,   0.000054,
        0.0008,     0.000677,   0.000409,   0.000176,   0.000054,   0.000012
        );

void main() 
{
    vec2 texelSize = 1.0f / vec2(textureSize(texSampler, 0));

    vec3 result = vec3(0.0f);

    for (int y = -5; y < 6; ++y)
    {
        for (int x = -5; x < 6; ++x)
        {
            vec2 stride = texelSize * vec2(float(x), float(y));
            float weight = kernel[abs(y) * 6 + abs(x)];
            result += texture(texSampler, vInput.uv + stride).rgb * weight;
        }
    }

    color = vec4(result, 1.0f);
}

