#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aVel;
layout(location = 2) in float aRoll;
layout(location = 3) in float aNForce;
layout(location = 4) in float aLForce;
layout(location = 5) in float aFlex;
layout(location = 6) in float aselected;
layout(location = 7) in vec3 aNormal;
layout(location = 8) in vec2 aUv;

layout(location = 0) out vec4 bPosition;
layout(location = 1) out vec3 bNormal;
layout(location = 2) out vec3 color;
layout(location = 3) out vec2 bUv;

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

vec3 getColor()
{
    switch (u.colorMode) {
        case 0: // nothing
            if(aselected < 0.5) return u.defaultColor.rgb;
            else if(aselected < 1.5) return u.sectionColor.rgb;
            else return u.transitionColor.rgb;
        case 1: // velocity
            if(aVel > 60.) return vec3(1., 0., 1.);
            else if(aVel >= 40.) return vec3(1., 0., (aVel-40.)/20);
            else if(aVel >= 30.) return vec3(1., (40.-aVel)/10., 0.);
            else if(aVel >= 20.) return vec3((aVel-20.)/10., 1., 0);
            else if(aVel >= 10.) return vec3(0., 1., (20-aVel)/10.);
            else if(aVel >= 1) return vec3(0., (aVel-1)/9, 1.);
            else return vec3(0., 0., 0.);
        case 2: // rollspeed
            if(aRoll > 240.) return vec3(0., 0., 0.);
            else if(aRoll >= 160) return vec3((240-aRoll)/80, 0., (240-aRoll)/80);
            else if(aRoll >= 80) return vec3(1., 0., (aRoll-80)/80);
            else if(aRoll >= 40) return vec3(1., (80-aRoll)/40, 0);
            else if(aRoll >= 20) return vec3((aRoll-20)/20, 1., 0.);
            else if(aRoll >= 10) return vec3(0., 1., (20.-aRoll)/10);
            else return vec3(0., aRoll/10, 1.);
        case 3: // normal force
            if(aNForce > 6.5) return vec3(0., 0., 0.);
            else if(aNForce > 5.) return vec3((6.5-aNForce)/1.5, 0., (6.5-aNForce)/1.5);
            else if(aNForce >= 3.5) return vec3(1., 0., (aNForce-3.5)/1.5);
            else if(aNForce >= 2) return vec3(1., (3.5-aNForce)/1.5, 0.);
            else if(aNForce >= 1.) return vec3(aNForce-1, 1., 0.);
            else if(aNForce >= 0.) return vec3(0., 1., 1-aNForce);
            else if(aNForce >= -1.) return vec3(0., aNForce+1., 1.);
            else if(aNForce >= -2.5) return vec3(0., 0., (aNForce+2.5)/(1.5));
            else return vec3(0., 0., 0.);
        case 4: // lateral force
            if(aLForce > 2.) return vec3(0., 0., 0.);
            else if(aLForce >= 1.5) return vec3((2-aLForce)/0.5, 0., (2-aLForce)/0.5);
            else if(aLForce >= 1.) return vec3(1., 0., (aLForce-1.0)/0.5);
            else if(aLForce >= 0.5) return vec3(1., (1.0-aLForce)/0.5, 0);
            else if(aLForce >= 0.25) return vec3((aLForce-0.25)/0.25, 1., 0.);
            else if(aLForce >= 0.1) return vec3(0., 1., (0.25-aLForce)/0.15);
            else return vec3(0., aLForce*10, 1.);
        case 5: // flexion
            if(aFlex > 30.) return vec3(0., 0., 0.);
            else if(aFlex >= 6) return vec3((30-aFlex)/24, 0., (30-aFlex)/24);
            else if(aFlex >= 4.5) return vec3(1., 0., (aFlex-4.5)/1.5);
            else if(aFlex >= 3.5) return vec3(1., (4.5-aFlex)/1, 0);
            else if(aFlex >= 2.5) return vec3((aFlex-2.5)/1, 1., 0.);
            else if(aFlex >= 1.0) return vec3(0., 1., (2.5-aFlex)/1.5);
            else return vec3(0., aFlex, 1.);
        default:
            return u.defaultColor.rgb;
    }
}

void main(void)
{
    color = getColor();
    bPosition = u.anchorBase * vec4(aPosition, 1);
    gl_Position = u.projectionMatrix * u.modelMatrix * bPosition;
    bPosition -= vec4(u.eyePos.xyz, 0);
    bNormal = vec3(u.anchorBase * vec4(aNormal, 0));
    if(length(bNormal) < 0.5) bNormal = vec3(0, 1, 0);
    bUv = aUv;
}
