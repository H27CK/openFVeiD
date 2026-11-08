#version 450

layout(location = 0) in vec3 vNear;
layout(location = 1) in vec3 vFar;

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
    float gridFadeStart;
    float gridFadeEnd;
}
u;

layout(set = 0, binding = 1) uniform sampler2D floorTex;

// Procedural antialiased screen-space line function for a periodic grid
float gridFactor(vec2 worldCoord, float spacing, float lineThickness) {
    vec2 uv = worldCoord / spacing;
    vec2 width = fwidth(uv);
    vec2 grid = abs(fract(uv + 0.5) - 0.5) / max(width, vec2(0.00001));
    float line = min(grid.x, grid.y);
    return 1.0 - clamp(line / lineThickness, 0.0, 1.0);
}

// Procedural antialiased screen-space line function for single axis lines
float axisLineFactor(float coord, float lineThickness) {
    float width = fwidth(coord);
    float line = abs(coord) / max(width, 0.00001);
    return 1.0 - clamp(line / lineThickness, 0.0, 1.0);
}

void main(void) {
    vec3 rayDir = normalize(vFar - vNear);
    vec3 rayOrigin = vNear;

    float dy = abs(rayDir.y) > 0.000001 ? rayDir.y : (rayDir.y >= 0.0 ? 0.000001 : -0.000001);
    float t = (u.floorHeight - rayOrigin.y) / dy;
    if (t <= 0.0)
        discard;

    vec3 worldPos = rayOrigin + rayDir * t;
    vec4 clipPos = u.projectionMatrix * (u.modelMatrix * vec4(worldPos, 1.0));
    gl_FragDepth = clipPos.z / clipPos.w;
    if (gl_FragDepth < 0.0 || gl_FragDepth > 1.0)
        discard;

    vec2 worldXZ = worldPos.zx;
    vec2 curFloorCoord = (worldXZ + vec2(u.grdTexSize)) / u.grdTexSize - vec2(0.5, 0.5);

    float dist = length(worldPos - u.eyePos.xyz);

    bool outsideBorder = (curFloorCoord.x > 1.0 || curFloorCoord.y > 1.0 ||
                          curFloorCoord.x < 0.0 || curFloorCoord.y < 0.0) &&
                         u.border == 1;

    // Decoupled surface color: inside border draws texture, outside border draws plain floor color
    vec3 surfaceColor;
    if (outsideBorder) {
        surfaceColor = u.floorColor.rgb;
    } else {
        surfaceColor = texture(floorTex, curFloorCoord).rgb * u.floorColor.rgb;
    }

    // Grid is drawn everywhere
    if (u.grid != 0) {
        // Defensive fallbacks to prevent division by zero if options.cfg has uninitialized/empty thickness values
        float minorThickness = u.gridMinorThickness > 0.05 ? u.gridMinorThickness : 1.0;
        float majorThickness = u.gridMajorThickness > 0.05 ? u.gridMajorThickness : 1.5;

        // User-configurable distance falloff using smootherstep (C2 continuous quintic hermite)
        float fadeStart = max(u.gridFadeStart, 1.0);
        float fadeEnd = max(u.gridFadeEnd, fadeStart + 1.0);

        // Blender-style grazing angle falloff to seamlessly eliminate horizon line bunching
        vec3 V = (dist > 0.0001) ? normalize(u.eyePos.xyz - worldPos) : vec3(0.0, 1.0, 0.0);
        float cosAngle = clamp(abs(V.y), 0.0, 1.0);
        float angleFade = 1.0 - pow(1.0 - cosAngle, 3.0);

        // Minor grid lines fade out earlier across the first portion of the range
        float minorStart = fadeStart * 0.25;
        float tMinor = clamp((dist - minorStart) / (fadeStart - minorStart), 0.0, 1.0);
        float minorDistFade = 1.0 - (tMinor * tMinor * tMinor * (tMinor * (tMinor * 6.0 - 15.0) + 10.0));

        // Major grid lines and axis lines fade out across [fadeStart, fadeEnd] with smootherstep
        float tMajor = clamp((dist - fadeStart) / (fadeEnd - fadeStart), 0.0, 1.0);
        float majorDistFade = 1.0 - (tMajor * tMajor * tMajor * (tMajor * (tMajor * 6.0 - 15.0) + 10.0));

        float tAxis = clamp((dist - fadeStart) / ((fadeEnd * 1.25) - fadeStart), 0.0, 1.0);
        float axisDistFade = 1.0 - (tAxis * tAxis * tAxis * (tAxis * (tAxis * 6.0 - 15.0) + 10.0));

        // Combined Blender angle + distance falloff
        float minorFade = angleFade * minorDistFade;
        float majorFade = angleFade * majorDistFade;
        float axisFade = angleFade * axisDistFade;

        vec2 gridPos = vec2(worldPos.z, worldPos.x);

        // Minor grid lines (1m intervals)
        float minor = gridFactor(gridPos, 1.0, minorThickness) * minorFade;
        // Major grid lines (10m intervals, coinciding with axes at 0, 10, 20...)
        float major = gridFactor(gridPos, 10.0, majorThickness) * majorFade;

        // Colored axis lines extending out from origin (X = Red along Z=0, Z = Blue along X=0)
        float axisThickness = max(majorThickness, 1.5);
        float xAxis = axisLineFactor(worldPos.z, axisThickness) * axisFade;
        float zAxis = axisLineFactor(worldPos.x, axisThickness) * axisFade;

        vec3 xAxisCol = (worldPos.x >= 0.0) ? vec3(0.85, 0.20, 0.20) : vec3(0.50, 0.15, 0.15);
        vec3 zAxisCol = (worldPos.z >= 0.0) ? vec3(0.20, 0.45, 0.90) : vec3(0.15, 0.25, 0.60);

        // Blend the custom grid color over the surface color
        vec3 gridColor = u.gridColor.rgb;
        surfaceColor = mix(surfaceColor, gridColor, minor * 0.5);
        surfaceColor = mix(surfaceColor, gridColor, major * 1.0);

        // Blend colored axis lines over the grid
        surfaceColor = mix(surfaceColor, xAxisCol, xAxis);
        surfaceColor = mix(surfaceColor, zAxisCol, zAxis);
    }

    // Apply standard ambient + directional lighting consistently across the ground plane
    float nDotL = max(dot(vec3(0.0, 1.0, 0.0), -u.lightDir.xyz), 0.0);
    vec3 ambientLight = surfaceColor * u.ambientColor.rgb * u.ambientStrength;
    vec3 directionalLight = surfaceColor * u.sunColor.rgb * u.sunStrength * nDotL * 0.5;
    vec3 finalColor = ambientLight + directionalLight;

    // Apply mist
    if (u.mistEnabled != 0) {
        float mistRange = max(u.mistFar - u.mistNear, 0.0001);
        float mistFactor = clamp((dist - u.mistNear) / mistRange, 0.0, 1.0);
        finalColor = mix(finalColor, u.mistColor.rgb, mistFactor);
    }

    oFragColor = vec4(clamp(finalColor, 0.0, 1.0), u.opacity);
}
