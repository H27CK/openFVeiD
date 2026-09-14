#version 450

layout(location = 0) in float vHeight;

layout(location = 0) out vec4 oFragColor;

layout(set = 0, binding = 0) uniform SimpleShadowUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    mat4 shadowMatrix;
    vec4 shadowColor;
    float uTrackLength;
    float heartline;
    int isInstanced;
    int isAsset;
    float shadowStrength;
    float floorHeight;
    int softShadowsEnabled;
    float padding2;
}
u;

void main() {
    float heightFade = 1.0;
    if (u.softShadowsEnabled == 1) {
        // Fade the shadow opacity as a function of height above the ground plane:
        // Close to ground (height = 0) -> opacity is 100% of shadowStrength
        // High up (height >= 40m) -> opacity is fainted down to 15%
        heightFade = clamp(1.0 - (max(vHeight, 0.0) / 40.0), 0.15, 1.0);
    }

    // Use the customizable shadow color while following the effective sun intensity and height fade.
    oFragColor = vec4(u.shadowColor.rgb, 0.6 * u.shadowStrength * heightFade);
}
