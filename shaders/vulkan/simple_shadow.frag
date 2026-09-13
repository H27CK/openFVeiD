#version 450

layout(location = 0) out vec4 oFragColor;

layout(set = 0, binding = 0) uniform SimpleShadowUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    mat4 shadowMatrix;
    float uTrackLength;
    float heartline;
    int isInstanced;
    int isAsset;
    float shadowStrength;
    float padding0;
    float padding1;
    float padding2;
} u;

void main()
{
    // Preserve the established shadow color while following the effective sun intensity.
    oFragColor = vec4(0.2, 0.2, 0.2, 0.6 * u.shadowStrength);
}
