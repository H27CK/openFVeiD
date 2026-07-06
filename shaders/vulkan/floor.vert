#version 450

layout(location = 0) in vec3 aPosition;

layout(location = 0) out vec2 rasterCoord;
layout(location = 1) out vec2 floorCoord;
layout(location = 2) out vec4 bPosition;

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

void main(void)
{
    vec3 eye = u.eyePos.xyz;
    eye.y = 0.0;
    bPosition = vec4(aPosition.x, aPosition.y - u.eyePos.y, aPosition.z, 0.0);

    // Eliminating large world translation additions and matrix subtractions to resolve POV/ortho jittering.
    // In camera space: pos = R * (WorldPos - CameraPos) = R * (aPosition + eye - eyePos) = R * (aPosition - vec3(0, eyePos.y, 0))
    mat4 viewRot = u.modelMatrix;
    viewRot[3] = vec4(0.0, 0.0, 0.0, 1.0);
    vec3 cameraRelativePos = aPosition - vec3(0.0, u.eyePos.y, 0.0);
    gl_Position = u.projectionMatrix * viewRot * vec4(cameraRelativePos, 1.0);

    rasterCoord = 0.1 * (aPosition.zx + eye.zx + vec2(5.0, 5.0));
    floorCoord = (aPosition.zx + eye.zx + vec2(u.grdTexSize, u.grdTexSize)) / (u.grdTexSize) - vec2(0.5, 0.5);
}
