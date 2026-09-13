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
    vec4 lightDir;
    vec4 ambientColor;
    vec4 sunColor;
    float floorHeight;
    float grdTexSize;
    float opacity;
    int border;
    int grid;
    int mistEnabled;
    float mistNear;
    float mistFar;
    float ambientStrength;
    float sunStrength;
    float padding0;
    float padding1;
} u;

layout(set = 0, binding = 1) uniform sampler2D rasterTex;
layout(set = 0, binding = 2) uniform sampler2D floorTex;

void main(void)
{
    vec3 surfaceColor = texture(floorTex, floorCoord).rgb * u.floorColor.rgb;
    bool outsideBorder = (floorCoord.x > 1 || floorCoord.y > 1 ||
                          floorCoord.x < 0 || floorCoord.y < 0) && u.border == 1;

    // Border logic: outside main area, use mistColor directly
    if (outsideBorder)
        surfaceColor = u.mistColor.rgb;

    if (u.grid == 1 && !outsideBorder)
        surfaceColor *= texture(rasterTex, rasterCoord).x;

    vec3 finalColor;
    if (outsideBorder) {
        finalColor = surfaceColor * 0.8;
    } else {
        float nDotL = max(dot(vec3(0.0, 1.0, 0.0), -u.lightDir.xyz), 0.0);
        vec3 ambientLight = surfaceColor * u.ambientColor.rgb * u.ambientStrength;
        vec3 directionalLight = surfaceColor * u.sunColor.rgb * u.sunStrength *
                                nDotL * 0.5;
        finalColor = ambientLight + directionalLight;
    }

    // Apply mist
    if (u.mistEnabled != 0) {
        // bPosition is in camera-relative world space
        float dist = length(bPosition.xyz);
        float mistRange = max(u.mistFar - u.mistNear, 0.0001);
        float mistFactor = clamp((dist - u.mistNear) / mistRange, 0.0, 1.0);
        finalColor = mix(finalColor, u.mistColor.rgb, mistFactor);
    }

    oFragColor = vec4(clamp(finalColor, 0.0, 1.0), u.opacity);
}
