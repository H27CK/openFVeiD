#version 450

layout(location = 0) in vec3 aPosition;

layout(location = 0) out vec3 vRayDir;

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
}
u;

void main(void) {
    // In camera space: pos = R * (WorldPos - CameraPos)
    mat4 viewRot = u.modelMatrix;
    viewRot[3] = vec4(0.0, 0.0, 0.0, 1.0);
    vec3 cameraRelativePos = aPosition + vec3(0.0, u.floorHeight - u.eyePos.y, 0.0);
    gl_Position = u.projectionMatrix * viewRot * vec4(cameraRelativePos, 1.0);

    // Pass the camera-relative ray direction for analytical per-pixel ray-plane reconstruction
    vRayDir = cameraRelativePos;
}
