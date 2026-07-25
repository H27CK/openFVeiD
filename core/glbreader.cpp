/*
#    FVD++, an advanced coaster design tool
#    Copyright (C) 2026 Veia <h27ck@proton.me>
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 3 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "glbreader.h"
#include "cgltf.h"
#include "stb_image.h"
#include "logger.h"
#include <iostream>

bool readGlb(const std::string& fileName, GlbModelData& modelData) {
    modelData.primitives.clear();

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, fileName.c_str(), &data);

    if (result != cgltf_result_success) {
        LOG_DEBUG("Failed to parse GLB: %s", fileName.c_str());
        return false;
    }

    result = cgltf_load_buffers(&options, data, fileName.c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        LOG_DEBUG("Failed to load GLB buffers: %s", fileName.c_str());
        return false;
    }

    for (cgltf_size nodeIdx = 0; nodeIdx < data->nodes_count; ++nodeIdx) {
        cgltf_node* node = &data->nodes[nodeIdx];
        if (!node->mesh) {
            continue;
        }

        // Compute world matrix for this node
        float worldMat[16];
        cgltf_node_transform_world(node, worldMat);
        glm::mat4 worldTransform(
            worldMat[0], worldMat[1], worldMat[2], worldMat[3],
            worldMat[4], worldMat[5], worldMat[6], worldMat[7],
            worldMat[8], worldMat[9], worldMat[10], worldMat[11],
            worldMat[12], worldMat[13], worldMat[14], worldMat[15]);

        cgltf_mesh* mesh = node->mesh;
        for (cgltf_size primIdx = 0; primIdx < mesh->primitives_count; ++primIdx) {
            cgltf_primitive* primitive = &mesh->primitives[primIdx];

            cgltf_accessor* posAccessor = nullptr;
            cgltf_accessor* normAccessor = nullptr;
            cgltf_accessor* uvAccessor = nullptr;
            cgltf_size numVertices = 0;

            for (cgltf_size attrIdx = 0; attrIdx < primitive->attributes_count; ++attrIdx) {
                cgltf_attribute* attr = &primitive->attributes[attrIdx];
                if (attr->type == cgltf_attribute_type_position) {
                    posAccessor = attr->data;
                    numVertices = posAccessor->count;
                } else if (attr->type == cgltf_attribute_type_normal) {
                    normAccessor = attr->data;
                } else if (attr->type == cgltf_attribute_type_texcoord) {
                    uvAccessor = attr->data;
                }
            }

            if (!posAccessor) {
                continue;
            }

            GlbPrimitiveData primData;
            primData.vertices.reserve(numVertices);

            for (cgltf_size i = 0; i < numVertices; ++i) {
                GlbVertex v;
                v.pos = glm::vec3(0.0f);
                v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                v.uv = glm::vec2(0.0f);

                float p[3] = {0.0f, 0.0f, 0.0f};
                cgltf_accessor_read_float(posAccessor, i, p, 3);
                glm::vec4 worldPos = worldTransform * glm::vec4(p[0], p[1], p[2], 1.0f);
                v.pos = glm::vec3(worldPos);

                modelData.minAABB = glm::min(modelData.minAABB, v.pos);
                modelData.maxAABB = glm::max(modelData.maxAABB, v.pos);

                if (normAccessor) {
                    float n[3] = {0.0f, 0.0f, 0.0f};
                    cgltf_accessor_read_float(normAccessor, i, n, 3);
                    glm::vec4 worldNorm = worldTransform * glm::vec4(n[0], n[1], n[2], 0.0f);
                    v.normal = glm::normalize(glm::vec3(worldNorm));
                }

                if (uvAccessor) {
                    float u[2] = {0.0f, 0.0f};
                    cgltf_accessor_read_float(uvAccessor, i, u, 2);
                    v.uv = glm::vec2(u[0], u[1]);
                }

                primData.vertices.push_back(v);
            }

            // Extract indices
            if (primitive->indices) {
                cgltf_accessor* idxAcc = primitive->indices;
                primData.indices.reserve(idxAcc->count);
                for (cgltf_size i = 0; i < idxAcc->count; ++i) {
                    primData.indices.push_back((uint32_t)cgltf_accessor_read_index(idxAcc, i));
                }
            } else {
                primData.indices.reserve(numVertices);
                for (cgltf_size i = 0; i < numVertices; ++i) {
                    primData.indices.push_back((uint32_t)i);
                }
            }

            // Extract material base color & textures
            if (primitive->material) {
                cgltf_material* material = primitive->material;
                if (material->has_pbr_metallic_roughness) {
                    primData.baseColorFactor = glm::vec4(
                        material->pbr_metallic_roughness.base_color_factor[0],
                        material->pbr_metallic_roughness.base_color_factor[1],
                        material->pbr_metallic_roughness.base_color_factor[2],
                        material->pbr_metallic_roughness.base_color_factor[3]);

                    cgltf_texture* texture = material->pbr_metallic_roughness.base_color_texture.texture;
                    if (texture && texture->image) {
                        cgltf_image* image = texture->image;
                        if (image->buffer_view) {
                            uint8_t* img_data = (uint8_t*)image->buffer_view->buffer->data + image->buffer_view->offset;
                            size_t img_size = image->buffer_view->size;

                            int width = 0, height = 0, channels = 0;
                            unsigned char* decoded = stbi_load_from_memory(img_data, (int)img_size, &width, &height, &channels, 4);
                            if (decoded) {
                                primData.hasTexture = true;
                                primData.textureData.width = width;
                                primData.textureData.height = height;
                                primData.textureData.rgba.assign(decoded, decoded + (size_t)width * height * 4);
                                stbi_image_free(decoded);
                            }
                        }
                    }
                }
            }

            modelData.primitives.push_back(std::move(primData));
        }
    }

    cgltf_free(data);
    return !modelData.primitives.empty();
}
