#version 450

layout(location = 0) in vec3 texCoord;

layout(location = 0) out vec4 oFragColor;

layout(set = 0, binding = 0) uniform SkyUniforms {
    vec4 topLeft;
    vec4 topRight;
    vec4 bottomLeft;
    vec4 bottomRight;
    vec4 fallbackColor;
    int hasTexture;
    int padding0;
    int padding1;
    int padding2;
} u;

layout(set = 0, binding = 1) uniform samplerCube skyTex;

void main(void)
{
    vec3 color;
    if (u.hasTexture != 0) {
        color = texture(skyTex, normalize(texCoord)).rgb;
    } else {
        color = u.fallbackColor.rgb;
    }

    oFragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
