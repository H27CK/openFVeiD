#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 7) in vec3 aNormal;

layout(location = 0) out vec4 bPosition;
layout(location = 1) out vec3 bNormal;

layout(set = 0, binding = 0) uniform NormalsUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    vec4 eyePos;
} u;

void main(void)
{
    bPosition = u.anchorBase * vec4(aPosition, 1);
    gl_Position = u.projectionMatrix * u.modelMatrix * bPosition;
    bPosition -= vec4(u.eyePos.xyz, 0);
    bNormal = vec3(u.anchorBase * vec4(aNormal, 0));
}
