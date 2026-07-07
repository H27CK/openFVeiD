#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
layout(location = 10) in float aFaceRefZ;

// Instance data
layout(location = 3) in mat4 aInstanceMatrix;
layout(location = 7) in vec4 aAttributes1;    // x=selected (instance), y=vel, z=rollSpeed, w=yForce
layout(location = 8) in vec4 aAttributes2;    // x=xForce, y=flexion, z=latOff, w=normOff
layout(location = 9) in vec3 aColor;

layout(location = 0) out vec4 bPosition;
layout(location = 1) out vec3 bNormal;
layout(location = 2) out vec3 color;
layout(location = 3) out vec2 bUv;

layout(set = 0, binding = 0) uniform TrackInstancedUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    vec4 eyePos;
    vec4 lightDir;
    vec4 defaultColor;
    vec4 sectionColor;
    vec4 transitionColor;
    vec4 mistColor;
    int colorMode;
    int mistEnabled;
    float mistNear;
    float mistFar;
    float uTrackLength;
    float heartline;
    int isAsset;
    int smoothAlongSpline;
    int padding;
} u;

struct SplineNode {
    vec4 pos;  // xyz=pos, w=totalLength
    vec4 lat;  // xyz=lat, w=selected
    vec4 norm;
    vec4 dir;
};

layout(std430, set = 1, binding = 0) readonly buffer SplineData {
    SplineNode nodes[];
};

vec3 getDynamicColor(float vSelected, float vVel, float vRoll, float vNForce, float vLForce, float vFlex) {
    if (vSelected > 1.5) return u.transitionColor.rgb; // Transition highlight (usually white)

    // If aColor is set to a specific value (not the default gray-ish 0.6 or 0.7), use it
    if (abs(aColor.r - 0.6) > 0.01 || abs(aColor.g - 0.6) > 0.01 || abs(aColor.b - 0.6) > 0.01) {
        if (abs(aColor.r - 0.7) > 0.01)
            return aColor;
    }

    switch (u.colorMode) {
        case 0: // standard
            if(vSelected < 0.5) return u.defaultColor.rgb;
            else if(vSelected < 1.5) return u.sectionColor.rgb;
            else return u.transitionColor.rgb;
        case 1: // velocity
            if(vVel > 60.) return vec3(1., 0., 1.);
            else if(vVel >= 40.) return vec3(1., 0., (vVel-40.)/20);
            else if(vVel >= 30.) return vec3(1., (40.-vVel)/10., 0.);
            else if(vVel >= 20.) return vec3((vVel-20.)/10., 1., 0);
            else if(vVel >= 10.) return vec3(0., 1., (20-vVel)/10.);
            else if(vVel >= 1) return vec3(0., (vVel-1)/9, 1.);
            else return vec3(0., 0., 0.);
        case 2: { // rollspeed
            float aRoll = abs(vRoll);
            if (aRoll > 240.) return vec3(0., 0., 0.);
            else if (aRoll >= 160) return vec3((240-aRoll)/80, 0., (240-aRoll)/80);
            else if (aRoll >= 80) return vec3(1., 0., (aRoll-80)/80);
            else if (aRoll >= 40) return vec3(1., (80-aRoll)/40, 0);
            else if (aRoll >= 20) return vec3((aRoll-20)/20, 1., 0.);
            else if (aRoll >= 10) return vec3(0., 1., (20.-aRoll)/10);
            else return vec3(0., aRoll/10, 1.);
        }
        case 3: { // normal force
            float aNForce = vNForce;
            if (aNForce > 6.5) return vec3(0., 0., 0.);
            else if (aNForce > 5.) return vec3((6.5-aNForce)/1.5, 0., (6.5-aNForce)/1.5);
            else if (aNForce >= 3.5) return vec3(1., 0., (aNForce-3.5)/1.5);
            else if (aNForce >= 2) return vec3(1., (3.5-aNForce)/1.5, 0.);
            else if (aNForce >= 1.) return vec3(aNForce-1, 1., 0.);
            else if (aNForce >= 0.) return vec3(0., 1., 1-aNForce);
            else if (aNForce >= -1.) return vec3(0., aNForce+1., 1.);
            else if (aNForce >= -2.5) return vec3(0., 0., (aNForce+2.5)/(1.5));
            else return vec3(0., 0., 0.);
        }
        case 4: { // lateral force
            float aLForce = abs(vLForce);
            if (aLForce > 2.) return vec3(0., 0., 0.);
            else if (aLForce >= 1.5) return vec3((2-aLForce)/0.5, 0., (2-aLForce)/0.5);
            else if (aLForce >= 1.) return vec3(1., 0., (aLForce-1.0)/0.5);
            else if (aLForce >= 0.5) return vec3(1., (1.0-aLForce)/0.5, 0);
            else if (aLForce >= 0.25) return vec3((aLForce-0.25)/0.25, 1., 0.);
            else if (aLForce >= 0.1) return vec3(0., 1., (0.25-aLForce)/0.15);
            else return vec3(0., aLForce*10, 1.);
        }
        case 5: { // flexion
            float aFlex = abs(vFlex);
            if (aFlex > 30.) return vec3(0., 0., 0.);
            else if (aFlex >= 6) return vec3((30-aFlex)/24, 0., (30-aFlex)/24);
            else if (aFlex >= 4.5) return vec3(1., 0., (aFlex-4.5)/1.5);
            else if (aFlex >= 3.5) return vec3(1., (4.5-aFlex)/1, 0);
            else if (aFlex >= 2.5) return vec3((aFlex-2.5)/1, 1., 0.);
            else if (aFlex >= 1.0) return vec3(0., 1., (2.5-aFlex)/1.5);
            else return vec3(0., aFlex, 1.);
        }
        default: return aColor;
    }
}

void main() {
    // Basic Attributes from instance
    bUv = aUv;

    // Spline Lookup
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
    vec3 iDir = normalize(mix(nodes[i0].dir.xyz, nodes[i1].dir.xyz, t));

    float normalRefZ = (u.smoothAlongSpline == 0) ? aFaceRefZ : aPosition.z;
    float normalDist = startDist + (normalRefZ - minZ);
    normalDist = clamp(normalDist, 0.0, u.uTrackLength);
    float fNormalIndex = normalDist / 0.1;
    int n0 = int(floor(fNormalIndex));
    int n1 = n0 + 1;
    float nt = fract(fNormalIndex);

    vec3 nLat = normalize(mix(nodes[n0].lat.xyz, nodes[n1].lat.xyz, nt));
    vec3 nNorm = normalize(mix(nodes[n0].norm.xyz, nodes[n1].norm.xyz, nt));
    vec3 nDir = normalize(mix(nodes[n0].dir.xyz, nodes[n1].dir.xyz, nt));

    // Sample selection state per-vertex
    float selected = mix(nodes[i0].lat.w, nodes[i1].lat.w, t);

    // Use instance-level physics values if we don't have per-node data (for standard coloring)
    float velocity = aAttributes1.y;
    float rollSpeed = aAttributes1.z;
    float normalForce = aAttributes1.w;
    float lateralForce = aAttributes2.x;
    float flexion = aAttributes2.y;

    color = getDynamicColor(selected, velocity, rollSpeed, normalForce, lateralForce, flexion);

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
    vec4 worldPos = u.anchorBase * vec4(warpedPos, 1.0);

    vec3 localNormal = aNormal;
    if (shouldInvert) localNormal = -localNormal;

    vec3 warpedNormal = normalize(-(nLat * localNormal.x) - (nNorm * localNormal.y) + (nDir * localNormal.z));
    bNormal = mat3(u.anchorBase) * warpedNormal;

    bPosition = worldPos - vec4(u.eyePos.xyz, 0.0);
    
    gl_Position = u.projectionMatrix * u.modelMatrix * worldPos;
}
