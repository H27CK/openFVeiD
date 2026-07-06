#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 7) in vec3 aNormal;

layout(location = 0) out vec4 bPosition;
layout(location = 1) out vec3 bNormal;
layout(location = 2) out vec3 baryCoord;

layout(set = 0, binding = 0) uniform StlUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    vec4 eyePos;
    vec4 lightDir;
    vec4 solidColor;
    vec4 mistColor;
    int wire;
    float edgeWidth;
    int mistEnabled;
    float mistNear;
    float mistFar;
    float padding0;
    float padding1;
    float padding2;
} u;

void main(void) {
    // generate barycentric coordinates based on vertex ID
    if (gl_VertexIndex % 3 == 0) {
        baryCoord = vec3(1.f, 0.f, 0.f);
    } else if (gl_VertexIndex % 3 == 1) {
        baryCoord = vec3(0.f, 1.f, 0.f);
    } else {
        baryCoord = vec3(0.f, 0.f, 1.f);
    }

    bPosition = u.anchorBase * vec4(aPosition, 1.f);
    gl_Position = u.projectionMatrix * u.modelMatrix * bPosition;
    bPosition -= vec4(u.eyePos.xyz, 0.f);
    bNormal = vec3(u.anchorBase * vec4(aNormal, 0.f));
    if (length(bNormal) < 0.5f)
        bNormal = vec3(0.f, 1.f, 0.f);
}
