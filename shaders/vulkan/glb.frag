#version 450

layout(location = 0) in vec4 bPosition;
layout(location = 1) in vec3 bNormal;
layout(location = 2) in vec3 baryCoord;
layout(location = 3) in vec2 bUv;

layout(location = 0) out vec4 oFragColor;

layout(set = 0, binding = 0) uniform GlbUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    vec4 eyePos;
    vec4 lightDir;
    vec4 solidColor;
    vec4 mistColor;
    vec4 ambientColor;
    vec4 sunColor;
    int wire;
    float edgeWidth;
    int mistEnabled;
    float mistNear;
    float mistFar;
    float ambientStrength;
    float sunStrength;
    float padding0;
} u;

layout(set = 0, binding = 1) uniform sampler2D uTexture;

void main(void)
{
    vec4 texColor = texture(uTexture, bUv);
    vec3 m_color = u.solidColor.rgb * texColor.rgb;
    float alpha = u.solidColor.a * texColor.a;

    vec3 normal = normalize(bNormal);
    float diffusal = max(dot(normal, -u.lightDir.xyz), 0.0);

    vec3 ambientLight = m_color * u.ambientColor.rgb * u.ambientStrength;
    vec3 directionalLight = m_color * u.sunColor.rgb * u.sunStrength *
                            diffusal * 0.5;
    vec3 finalColor = ambientLight + directionalLight;

    // Apply mist
    if (u.mistEnabled != 0) {
        float dist = length(bPosition.xyz);
        float mistFactor = clamp((dist - u.mistNear) / (u.mistFar - u.mistNear), 0.0, 1.0);
        finalColor = mix(finalColor, u.mistColor.rgb, mistFactor);
    }

    oFragColor = vec4(clamp(finalColor, 0.0, 1.0), alpha);

    if (u.wire == 1) {
        float edgeDist = min(min(baryCoord.x, baryCoord.y), baryCoord.z);
        if (edgeDist < u.edgeWidth) {
            oFragColor = vec4(0.2, 0.2, 0.2, 1.0);
        }
    }
}
