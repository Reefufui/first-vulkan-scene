#version 450

layout(set = 0, binding = 0) uniform sampler2D fireSampler;

layout (location = 0) in VOUT
{
    vec4 color;
    float alpha;
    float rotation;
} vInput;

layout(location = 0) out vec4 color;

void main()
{
    float alpha = (vInput.alpha <= 1.0f) ? vInput.alpha : 2.0f - vInput.alpha;

    float center = 0.5f; // of sapmpler
    float cosinus = cos(vInput.rotation);
    float sinus = sin(vInput.rotation);

    vec2 uv = center + (gl_PointCoord.xy - center) * cosinus + vec2(1.0f, -1.0f) * (gl_PointCoord.yx - center) * sinus;

    color = texture(fireSampler, uv) * vInput.color * vInput.alpha;
    color.w = 0.0f;
}
