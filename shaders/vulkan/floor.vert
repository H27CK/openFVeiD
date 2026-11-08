#version 450

layout(location = 0) in vec3 aPosition;

layout(location = 0) out vec3 vNear;
layout(location = 1) out vec3 vFar;

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

void main(void) {
    gl_Position = vec4(aPosition.xy, 1.0, 1.0);
    mat4 invPV = inverse(u.projectionMatrix * u.modelMatrix);
    vec4 nearPt = invPV * vec4(aPosition.xy, -1.0, 1.0);
    vec4 farPt = invPV * vec4(aPosition.xy, 1.0, 1.0);
    vNear = nearPt.xyz / nearPt.w;
    vFar = farPt.xyz / farPt.w;
}
