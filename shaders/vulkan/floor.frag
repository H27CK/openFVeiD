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
    vec4 gridColor;
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
    float gridMajorThickness;
    float gridMinorThickness;
} u;

layout(set = 0, binding = 1) uniform sampler2D rasterTex;
layout(set = 0, binding = 2) uniform sampler2D floorTex;

// Procedural antialiased screen-space line function
float gridFactor(vec2 uv, float lineThickness) {
    vec2 width = fwidth(uv);
    vec2 grid = abs(fract(uv - 0.5) - 0.5) / width;
    float line = min(grid.x, grid.y);
    return 1.0 - clamp(line / lineThickness, 0.0, 1.0);
}

void main(void)
{
    bool outsideBorder = (floorCoord.x > 1.0 || floorCoord.y > 1.0 ||
                          floorCoord.x < 0.0 || floorCoord.y < 0.0) && u.border == 1;

    // Decoupled surface color: inside border draws texture, outside border draws plain floor color
    vec3 surfaceColor;
    if (outsideBorder) {
        surfaceColor = u.floorColor.rgb;
    } else {
        surfaceColor = texture(floorTex, floorCoord).rgb * u.floorColor.rgb;
    }

    // Grid is drawn everywhere (extends to infinity!)
    if (u.grid == 1) {
        // Defensive fallbacks to prevent division by zero if options.cfg has uninitialized/empty thickness values
        float minorThickness = u.gridMinorThickness > 0.05 ? u.gridMinorThickness : 1.0;
        float majorThickness = u.gridMajorThickness > 0.05 ? u.gridMajorThickness : 1.5;

        // Minor grid lines (1m intervals, period is 0.1 in rasterCoord space)
        float minor = gridFactor(rasterCoord * 10.0, minorThickness); // customizable screen-space thickness
        // Major grid lines (10m intervals, period is 1.0 in rasterCoord space)
        float major = gridFactor(rasterCoord, majorThickness); // customizable screen-space thickness

        // Blend the custom grid color over the surface color
        vec3 gridColor = u.gridColor.rgb;
        surfaceColor = mix(surfaceColor, gridColor, minor * 0.5);
        surfaceColor = mix(surfaceColor, gridColor, major * 1.0);
    }

    // Apply standard ambient + directional lighting consistently across the infinite ground plane
    float nDotL = max(dot(vec3(0.0, 1.0, 0.0), -u.lightDir.xyz), 0.0);
    vec3 ambientLight = surfaceColor * u.ambientColor.rgb * u.ambientStrength;
    vec3 directionalLight = surfaceColor * u.sunColor.rgb * u.sunStrength * nDotL * 0.5;
    vec3 finalColor = ambientLight + directionalLight;

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
