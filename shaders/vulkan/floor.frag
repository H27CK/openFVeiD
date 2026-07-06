#version 450

layout(location = 0) in vec2 rasterCoord;
layout(location = 1) in vec2 floorCoord;
layout(location = 2) in vec4 bPosition;

layout(location = 0) out vec4 oFragColor;

layout(set = 0, binding = 0) uniform FloorUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    vec4 eyePos;
    vec4 floorColor;
    vec4 mistColor;
    float grdTexSize;
    float opacity;
    int border;
    int grid;
    int mistEnabled;
    float mistNear;
    float mistFar;
    float padding;
} u;

layout(set = 0, binding = 1) uniform sampler2D rasterTex;
layout(set = 0, binding = 2) uniform sampler2D floorTex;

void main(void)
{
    oFragColor = texture(floorTex, floorCoord) * vec4(u.floorColor.rgb, 1.0);
    oFragColor.w = u.opacity;

    // Border logic: outside main area, use mistColor directly
    if ((floorCoord.x > 1 || floorCoord.y > 1 || floorCoord.x < 0 || floorCoord.y < 0) && u.border == 1)
    {
        oFragColor.xyz = u.mistColor.rgb;
    }

    if (u.grid == 1) oFragColor.xyz *= (texture(rasterTex, rasterCoord).x);

    // Standard Ambient lighting model
    vec3 finalColor = oFragColor.xyz * 0.8;

    // Apply mist
    if (u.mistEnabled != 0) {
        // bPosition is in camera-relative world space
        float dist = length(bPosition.xyz);
        float mistFactor = clamp((dist - u.mistNear) / (u.mistFar - u.mistNear), 0.0, 1.0);
        finalColor = mix(finalColor, u.mistColor.rgb, mistFactor);
    }

    oFragColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}
