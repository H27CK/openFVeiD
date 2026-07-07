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

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"
#include "customstyle.h"
#include "logger.h"
#include <iostream>
#include <algorithm>

CustomTrackStyle::CustomTrackStyle()
    : length(0.0f), isValid(false) {}

CustomTrackStyle::~CustomTrackStyle() {}

bool CustomTrackStyle::load(const std::string& filepath) {
    vertices.clear();
    indices.clear();
    length = 0.0f;
    isValid = false;

    cgltf_options options = {};
    cgltf_data* data = NULL;
    cgltf_result result = cgltf_parse_file(&options, filepath.c_str(), &data);

    if (result != cgltf_result_success) {
        LOG_DEBUG("Failed to parse GLTF: %s", filepath.c_str());
        return false;
    }

    result = cgltf_load_buffers(&options, data, filepath.c_str());
    if (result != cgltf_result_success) {
        cgltf_free(data);
        LOG_DEBUG("Failed to load GLTF buffers: %s", filepath.c_str());
        return false;
    }

    if (data->meshes_count == 0 || data->meshes[0].primitives_count == 0) {
        cgltf_free(data);
        LOG_DEBUG("No meshes found in GLTF: %s", filepath.c_str());
        return false;
    }

    cgltf_primitive* primitive = &data->meshes[0].primitives[0];

    // Extract Attributes
    int numVertices = 0;
    cgltf_accessor* posAccessor = nullptr;
    cgltf_accessor* normAccessor = nullptr;
    cgltf_accessor* uvAccessor = nullptr;

    for (cgltf_size i = 0; i < primitive->attributes_count; ++i) {
        cgltf_attribute* attr = &primitive->attributes[i];
        if (attr->type == cgltf_attribute_type_position) {
            numVertices = attr->data->count;
            posAccessor = attr->data;
        } else if (attr->type == cgltf_attribute_type_normal) {
            normAccessor = attr->data;
        } else if (attr->type == cgltf_attribute_type_texcoord) {
            uvAccessor = attr->data;
        }
    }

    if (!posAccessor) {
        cgltf_free(data);
        return false;
    }

    float minZ = 999999.0f;
    float maxZ = -999999.0f;

    for (int i = 0; i < numVertices; ++i) {
        StyleVertex v;
        v.pos = glm::vec3(0.0f);
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.uv = glm::vec2(0.0f);
        v.faceRefZ = 0.0f;

        float p[3] = {0.0f, 0.0f, 0.0f};
        cgltf_accessor_read_float(posAccessor, i, p, 3);
        v.pos = glm::vec3(p[0], p[1], p[2]);

        if (normAccessor) {
            float n[3] = {0.0f, 0.0f, 0.0f};
            cgltf_accessor_read_float(normAccessor, i, n, 3);
            v.normal = glm::vec3(n[0], n[1], n[2]);
        }

        if (uvAccessor) {
            float u[2] = {0.0f, 0.0f};
            cgltf_accessor_read_float(uvAccessor, i, u, 2);
            v.uv = glm::vec2(u[0], u[1]);
        }

        minZ = std::min(minZ, v.pos.z);
        maxZ = std::max(maxZ, v.pos.z);

        vertices.push_back(v);
    }

    length = maxZ - minZ;
    LOG_DEBUG("Model Loaded. Bounding Box Z: [%.2f, %.2f] Length: %.2f", minZ, maxZ, length);
    if (length < 0.01f)
        length = 1.0f; // Fallback to prevent divide by zero

    LOG_DEBUG("Extracting indices...");
    // Extract Indices
    if (primitive->indices) {
        cgltf_accessor* idxAcc = primitive->indices;
        LOG_DEBUG("Index accessor count: %d", idxAcc->count);
        for (cgltf_size i = 0; i < idxAcc->count; ++i) {
            unsigned int idx = cgltf_accessor_read_index(idxAcc, i);
            indices.push_back(idx);
        }
    } else {
        LOG_DEBUG("No indices, generating sequential...");
        for (int i = 0; i < numVertices; ++i) {
            indices.push_back(i);
        }
    }

    std::vector<StyleVertex> expandedVertices;
    std::vector<unsigned int> expandedIndices;
    expandedVertices.reserve(indices.size());
    expandedIndices.reserve(indices.size());

    for (size_t t = 0; t + 2 < indices.size(); t += 3) {
        unsigned int i0 = indices[t];
        unsigned int i1 = indices[t + 1];
        unsigned int i2 = indices[t + 2];

        float avgZ = (vertices[i0].pos.z + vertices[i1].pos.z + vertices[i2].pos.z) / 3.0f;

        unsigned int corners[3] = {i0, i1, i2};
        for (unsigned int corner : corners) {
            StyleVertex v = vertices[corner];
            v.faceRefZ = avgZ;
            expandedVertices.push_back(v);
            expandedIndices.push_back((unsigned int)expandedVertices.size() - 1);
        }
    }

    vertices.swap(expandedVertices);
    indices.swap(expandedIndices);

    LOG_DEBUG("Freeing data...");
    cgltf_free(data);
    isValid = true;
    LOG_DEBUG("Load successful.");
    return true;
}