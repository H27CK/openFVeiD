#version 450

layout(location = 0) in vec3 aPosition;

// Instance data
layout(location = 3) in mat4 aInstanceMatrix;
layout(location = 7) in vec4 aAttributes1;
layout(location = 8) in vec4 aAttributes2;

layout(set = 0, binding = 0) uniform SimpleShadowUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    mat4 shadowMatrix;
    float uTrackLength;
    float heartline;
    int isInstanced;
    int isAsset;
} u;

struct SplineNode {
    vec4 pos;
    vec4 lat;
    vec4 norm;
    vec4 dir;
};

layout(std430, set = 1, binding = 0) readonly buffer SplineData {
    SplineNode nodes[];
};

void main()
{
    vec4 worldPos;
    if (u.isInstanced == 1) {
        // Spline-Warped Path for Assets and Ties
        float startDist = aInstanceMatrix[3].x;
        float minZ = aInstanceMatrix[3].z;

        float dist = startDist + (aPosition.z - minZ);
        dist = clamp(dist, 0.0, u.uTrackLength);

        float fIndex = dist / 0.1;
        int i0 = int(floor(fIndex));
        int i1 = i0 + 1;
        float t = fract(fIndex);

        vec3 iPos = mix(nodes[i0].pos.xyz, nodes[i1].pos.xyz, t);
        vec3 iLat = normalize(mix(nodes[i0].lat.xyz, nodes[i1].lat.xyz, t));
        vec3 iNorm = normalize(mix(nodes[i0].norm.xyz, nodes[i1].norm.xyz, t));

        float localX = aPosition.x * aInstanceMatrix[0][0];
        float localY = aPosition.y * aInstanceMatrix[1][1];

        float lateralOffset = aAttributes2.z;
        float normalOffset = aAttributes2.w;

        if (u.isAsset == 1) {
            normalOffset = 0.0;
        }

        bool shouldInvert = (u.heartline < 0.0);

        if (shouldInvert) {
            localX = -localX;
            localY = -localY;
            lateralOffset = -lateralOffset;
            normalOffset = -normalOffset;
        }

        float totalX = localX + lateralOffset;
        float totalY = localY + normalOffset;

        vec3 warpedPos = iPos - (iLat * totalX) - (iNorm * totalY);
        worldPos = u.anchorBase * vec4(warpedPos, 1.0);
    } else {
        // Standard Track Geometry
        worldPos = u.anchorBase * vec4(aPosition, 1.0);
    }

    // 1. Project WorldPos to Ground Plane (Shadow Space)
    vec4 shadowedWorldPos = u.shadowMatrix * worldPos;

    // 2. Transform to Camera View and Projection
    gl_Position = u.projectionMatrix * u.modelMatrix * shadowedWorldPos;
}
