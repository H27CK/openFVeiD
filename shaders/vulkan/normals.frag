#version 450

layout(location = 0) in vec4 bPosition;
layout(location = 1) in vec3 bNormal;

layout(location = 0) out vec4 normal;

void main(void)
{
    normal = vec4(bNormal, length(bPosition.xyz));
}
