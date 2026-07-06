#version 450

layout(location = 0) in vec3 aPosition;

layout(location = 0) out vec3 texCoord;

layout(set = 0, binding = 0) uniform SkyUniforms {
    vec4 topLeft;
    vec4 topRight;
    vec4 bottomLeft;
    vec4 bottomRight;
    vec4 fallbackColor;
    int hasTexture;
    int padding0;
    int padding1;
    int padding2;
} u;

void main(void)
{
    gl_Position = vec4(aPosition, 1.0);
    if (aPosition.x < -0.5)
    {// left
        if (aPosition.y < -0.5)
            texCoord = u.bottomLeft.xyz;
        else
            texCoord = u.topLeft.xyz;
    }
    else
    {// right
        if (aPosition.y < -0.5)
            texCoord = u.bottomRight.xyz;
        else
            texCoord = u.topRight.xyz;
    }
}
