#version 450 core

layout(location = 0) in VOUT
{
    vec3 color;
    vec3 normal;
    vec3 eyePos;
    vec3 lightVector;
    vec3 worldPos;
    vec3 lightPos;
    vec2 uv;
} vInput;

#define EPSILON 0.15
#define SHADOW_OPACITY 0.5

layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D   texSampler;
layout(binding = 1) uniform samplerCube shadowCubemap;

void main()
{
    vec3 N = normalize(vInput.normal);
    vec3 L = normalize(vec3(1.0f));
    vec3 Eye = normalize(-vInput.eyePos);
    vec3 Reflected = normalize(reflect(-vInput.lightVector, vInput.normal));

    vec4 IAmbient = vec4(vec3(0.05f), 1.0f);
    vec4 IDiffuse = vec4(1.0f) * max(dot(vInput.normal, vInput.lightVector), 0.0);

    color = IAmbient + texture(texSampler, vInput.uv) * IDiffuse;

/*
    vec3 lightVec = vInput.worldPos - vInput.lightPos;
    float sampledDist = texture(shadowCubemap, vInput.lightVector).r;
    float dist = length(lightVec);

    float shadow = (dist <= sampledDist + EPSILON) ? 1.0 : SHADOW_OPACITY;

    color.rgb *= shadow;
    */
}
