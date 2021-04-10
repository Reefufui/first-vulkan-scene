// created in 2021 by Andrey Treefonov https://github.com/Reefufui

#version 450 core

layout (location = 0) in VOUT
{
    vec3 normal;
    vec3 worldModel;
    vec3 worldLight;
    vec2 uv;
} vInput;

layout(location = 0) out vec4 color;

layout(set = 0, binding = 0) uniform sampler2D   texSampler;
layout(set = 1, binding = 0) uniform samplerCube shadowCubemap;

const float eps      = 0.15f;
const float shadow   = 0.5f;
const float pcfDelta = 0.05f;

float PCF(vec3 a_toLight)
{
    int size = 1;
    float sumShadow = 0.0f;

    for (int y = -size; y <= size; ++y)
    {
        for (int x = -size; x <= size; ++x)
        {
            for (int z = -size; z <= size; ++z)
            {
                vec3 lightVec = a_toLight + pcfDelta * vec3(x, y, z);

                if (length(lightVec) > texture(shadowCubemap, lightVec).r + eps)
                {
                    sumShadow += shadow; 
                }
                else
                {
                    sumShadow += 1.0f; 
                }
            }
        }
    }

    return sumShadow / ((2 * size + 1) * (2 * size + 1) * (2 * size + 1));
}

void main()
{
    vec3 toLight = vInput.worldLight -vInput.worldModel;

    vec4 diffuse = vec4(1.0f) * max(dot(vInput.normal, normalize(toLight)), 0.0f);

    color = vec4(0.1f) + diffuse * texture(texSampler, vInput.uv);

    color.rgb *= PCF(toLight);
}
