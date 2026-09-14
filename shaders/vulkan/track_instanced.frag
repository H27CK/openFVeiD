#version 450

layout(location = 0) in vec4 bPosition;
layout(location = 1) in vec3 bNormal;
layout(location = 2) in vec3 color;
layout(location = 4) in vec3 bMaterialPosition;
layout(location = 5) in vec3 bMaterialNormal;

layout(location = 0) out vec4 oFragColor;

layout(set = 0, binding = 0) uniform TrackInstancedUniforms {
    mat4 projectionMatrix;
    mat4 modelMatrix;
    mat4 anchorBase;
    vec4 eyePos;
    vec4 lightDir;
    vec4 defaultColor;
    vec4 sectionColor;
    vec4 transitionColor;
    vec4 mistColor;
    vec4 ambientColor;
    vec4 sunColor;
    int colorMode;
    int mistEnabled;
    float mistNear;
    float mistFar;
    float uTrackLength;
    float heartline;
    int isAsset;
    int smoothAlongSpline;
    float ambientStrength;
    float sunStrength;
    int materialEnabled;
    float padding1;
} u;

layout(set = 0, binding = 1) uniform sampler2D metalNormalMap;
layout(set = 0, binding = 2) uniform sampler2D metalRoughnessMap;

const float MATERIAL_TEXTURE_SCALE = 1.0;
const float MATERIAL_NORMAL_STRENGTH = 0.6;
const float MATERIAL_ROUGHNESS_SCALE = 1.0;
const float MATERIAL_GRAIN_CONTRAST = 1.0;

vec3 mappedNormal(vec2 materialUv, vec3 tangentNormalSample) {
    vec3 geometricNormal = normalize(bNormal);
    vec3 positionDx = dFdx(bPosition.xyz);
    vec3 positionDy = dFdy(bPosition.xyz);
    vec2 uvDx = dFdx(materialUv);
    vec2 uvDy = dFdy(materialUv);
    float determinant = uvDx.x * uvDy.y - uvDx.y * uvDy.x;

    // Degenerate UVs can occur on malformed input; retain regular surface
    // shading instead of introducing NaNs or a visible black triangle.
    if (abs(determinant) < 0.000001)
        return geometricNormal;

    vec3 tangent = (positionDx * uvDy.y - positionDy * uvDx.y) / determinant;
    vec3 rawBitangent = (positionDy * uvDx.x - positionDx * uvDy.x) / determinant;
    tangent = normalize(tangent - geometricNormal * dot(geometricNormal, tangent));
    float handedness = dot(cross(geometricNormal, tangent), rawBitangent) < 0.0 ? -1.0 : 1.0;
    vec3 bitangent = normalize(cross(geometricNormal, tangent)) * handedness;

    vec3 tangentNormal = tangentNormalSample * 2.0 - 1.0;
    tangentNormal.xy *= MATERIAL_NORMAL_STRENGTH;
    return normalize(mat3(tangent, bitangent, geometricNormal) * tangentNormal);
}

vec3 materialWeights() {
    vec3 weights = pow(abs(normalize(bMaterialNormal)), vec3(4.0));
    return weights / max(weights.x + weights.y + weights.z, 0.0001);
}

vec2 materialUvX() { return bMaterialPosition.zy * MATERIAL_TEXTURE_SCALE; }
vec2 materialUvY() { return bMaterialPosition.xz * MATERIAL_TEXTURE_SCALE; }
vec2 materialUvZ() { return bMaterialPosition.xy * MATERIAL_TEXTURE_SCALE; }

float sampleTriplanarRoughness(vec3 weights) {
    return texture(metalRoughnessMap, materialUvX()).r * weights.x +
           texture(metalRoughnessMap, materialUvY()).r * weights.y +
           texture(metalRoughnessMap, materialUvZ()).r * weights.z;
}

vec3 triplanarMappedNormal(vec3 weights, vec3 sampleX, vec3 sampleY, vec3 sampleZ) {
    vec3 normalX = mappedNormal(materialUvX(), sampleX);
    vec3 normalY = mappedNormal(materialUvY(), sampleY);
    vec3 normalZ = mappedNormal(materialUvZ(), sampleZ);
    return normalize(normalX * weights.x + normalY * weights.y + normalZ * weights.z);
}

vec3 applyMist(vec3 colorValue) {
    if (u.mistEnabled == 0)
        return colorValue;

    float distanceFromEye = length(bPosition.xyz);
    float mistRange = max(u.mistFar - u.mistNear, 0.0001);
    float mistFactor = clamp((distanceFromEye - u.mistNear) / mistRange, 0.0, 1.0);
    return mix(colorValue, u.mistColor.rgb, mistFactor);
}

float distributionGgx(float nDotH, float roughness) {
    float alpha = roughness * roughness;
    float alphaSquared = alpha * alpha;
    float denominator = nDotH * nDotH * (alphaSquared - 1.0) + 1.0;
    return alphaSquared / max(3.14159265 * denominator * denominator, 0.0001);
}

float geometrySchlickGgx(float nDotDirection, float roughness) {
    float radius = roughness + 1.0;
    float k = radius * radius / 8.0;
    return nDotDirection / max(nDotDirection * (1.0 - k) + k, 0.0001);
}

void main() {
    vec3 baseColor = color;
    vec3 lightDirection = normalize(-u.lightDir.xyz);

    if (u.materialEnabled == 0) {
        float nDotL = max(dot(normalize(bNormal), lightDirection), 0.0);
        vec3 ambientLight = baseColor * u.ambientColor.rgb * u.ambientStrength;
        vec3 directionalLight = baseColor * u.sunColor.rgb * u.sunStrength * nDotL * 0.5;
        oFragColor = vec4(clamp(applyMist(ambientLight + directionalLight), 0.0, 1.0), 1.0);
        return;
    }

    vec3 projectionWeights = materialWeights();
    vec3 normalSampleX = texture(metalNormalMap, materialUvX()).rgb;
    vec3 normalSampleY = texture(metalNormalMap, materialUvY()).rgb;
    vec3 normalSampleZ = texture(metalNormalMap, materialUvZ()).rgb;
    vec3 normalMapSample = normalSampleX * projectionWeights.x +
                           normalSampleY * projectionWeights.y +
                           normalSampleZ * projectionWeights.z;

    vec3 normal = triplanarMappedNormal(projectionWeights, normalSampleX,
                                        normalSampleY, normalSampleZ);
    vec3 viewVector = -bPosition.xyz;
    vec3 viewDirection = viewVector / max(length(viewVector), 0.0001);
    vec3 halfwayVector = lightDirection + viewDirection;
    vec3 halfwayDirection = halfwayVector / max(length(halfwayVector), 0.0001);

    float nDotL = max(dot(normal, lightDirection), 0.0);
    float nDotV = max(dot(normal, viewDirection), 0.0);
    float nDotH = max(dot(normal, halfwayDirection), 0.0);
    float vDotH = max(dot(viewDirection, halfwayDirection), 0.0);
    float roughness = clamp(sampleTriplanarRoughness(projectionWeights) *
                            MATERIAL_ROUGHNESS_SCALE, 0.08, 1.0);

    float distribution = distributionGgx(nDotH, roughness);
    float geometry = geometrySchlickGgx(nDotV, roughness) *
                     geometrySchlickGgx(nDotL, roughness);
    vec3 reflectance = mix(vec3(0.04), baseColor, 0.55);
    vec3 fresnel = reflectance + (1.0 - reflectance) * pow(1.0 - vDotH, 5.0);
    vec3 specular = distribution * geometry * fresnel /
                    max(4.0 * nDotV * nDotL, 0.0001);

    vec3 ambientDiffuse = baseColor * u.ambientColor.rgb * u.ambientStrength;
    vec3 ambientFresnel = reflectance +
                          (1.0 - reflectance) * pow(1.0 - nDotV, 5.0);
    float ambientReflectionStrength = mix(0.35, 0.08, roughness);
    vec3 ambientSpecular = u.ambientColor.rgb * u.ambientStrength *
                           ambientFresnel * ambientReflectionStrength;

    vec3 directionalDiffuse = baseColor * u.sunColor.rgb * u.sunStrength *
                              nDotL * 0.38;
    vec3 directionalSpecular = specular * u.sunColor.rgb * u.sunStrength *
                               nDotL * 0.45;
    vec3 finalColor = ambientDiffuse + ambientSpecular +
                      directionalDiffuse + directionalSpecular;

    // Preserve the normal map's fine surface grain after antialiasing. This is
    // a neutral brightness response (not an albedo texture), tied to the same
    // strength and meter-based projection as the actual normal perturbation.
    vec2 grainDirection = normalize(vec2(0.63, 0.78));
    float grainSignal = dot(normalMapSample.rg - vec2(0.5), grainDirection) * 2.0;
    float grainLighting = clamp(1.0 + grainSignal * MATERIAL_NORMAL_STRENGTH *
                                MATERIAL_GRAIN_CONTRAST * 0.35, 0.72, 1.28);
    finalColor *= grainLighting;

    oFragColor = vec4(clamp(applyMist(finalColor), 0.0, 1.0), 1.0);
}
