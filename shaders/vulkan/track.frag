#version 450

layout(location = 0) in vec4 bPosition;
layout(location = 1) in vec3 bNormal;
layout(location = 2) in vec3 color;
layout(location = 3) in vec2 bUv;

layout(location = 0) out vec4 oFragColor;

layout(set = 0, binding = 0) uniform TrackUniforms {
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
} u;

void main(void)
{
    vec3 m_color = color;
    vec3 normal = normalize(bNormal);

    float diffusal = max(dot(normal, -u.lightDir.xyz), 0.0);

    // Clean CAD lighting model: Ambient + Diffuse
    float ambient = 0.8;

    vec3 finalColor = (ambient + diffusal * 0.5) * m_color;

    // Apply mist
    if (u.mistEnabled != 0) {
        float dist = length(bPosition.xyz);
        float mistFactor = clamp((dist - u.mistNear) / (u.mistFar - u.mistNear), 0.0, 1.0);
        finalColor = mix(finalColor, u.mistColor.rgb, mistFactor);
    }

    oFragColor = vec4(clamp(finalColor, 0.0, 1.0), 1.0);
}
