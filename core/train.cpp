/*
#    FVD++, an advanced coaster design tool
#    Copyright (C) 2026 Veia <h27ck@proton.me>
#    Copyright (C) 2026 Ercan Akyürek <ercan.akyuerek@gmail.com>
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

#include "train.h"
#include "customtrack.h"
#include "glbreader.h"
#include "track.h"
#include "imgui.h"
#include "vfs.h"
#include "logger.h"

#include <sstream>
#include <filesystem>
#include <cmath>
#include <algorithm>

Train::Train() = default;

Train::~Train() {
    clear();
}

void Train::clear() {
    for (auto& asset : assets) {
        if (asset.model) {
            delete asset.model;
            asset.model = nullptr;
        }
    }
    assets.clear();
    carDefs.clear();
    baseLayout.clear();
    layout.clear();
    cameras.clear();
    repeatingCarId.clear();
    filepath.clear();
    boundCameraIndex = -1;
    author.clear();
    manufacturer.clear();
    type.clear();
}

const TrainCarDef* Train::findCarDef(const std::string& carId) const {
    for (const auto& def : carDefs) {
        if (def.id == carId) {
            return &def;
        }
    }
    return nullptr;
}

float Train::getTotalLength() const {
    float total = 0.0f;
    for (const auto& carId : layout) {
        const auto* def = findCarDef(carId);
        if (def) {
            total += def->length;
        }
    }
    return total;
}

void Train::setMiddleCarCount(int count, track* parentTrack) {
    middleCarCount = std::max(0, count);
    rebuildLayout(parentTrack);
}

void Train::rebuildLayout(track* parentTrack) {
    layout.clear();
    if (baseLayout.empty())
        return;

    if (!repeatingCarId.empty() && baseLayout.size() >= 2) {
        layout.push_back(baseLayout.front());
        for (int i = 0; i < middleCarCount; ++i) {
            layout.push_back(repeatingCarId);
        }
        layout.push_back(baseLayout.back());
    } else {
        layout = baseLayout;
    }

    rebuildPoints(parentTrack);
}

void Train::rebuildPoints(track* parentTrack) {
    cameras.clear();
    if (parentTrack) {
        parentTrack->trainOffsets.clear();
    }

    float totalLen = getTotalLength();
    float halfLen = totalLen / 2.0f;

    size_t totalMPoints = 0;
    for (const auto& carId : layout) {
        const auto* def = findCarDef(carId);
        if (def) {
            totalMPoints += def->measurementDefs.size();
        }
    }
    if (totalMPoints == 0)
        totalMPoints = 1;

    size_t pointCounter = 0;
    double currentCarDistance = 0.0;

    for (size_t carIdx = 0; carIdx < layout.size(); ++carIdx) {
        const auto* carDef = findCarDef(layout[carIdx]);
        if (!carDef)
            continue;

        // Instantiate MPOINT measurement points on parent track
        if (parentTrack) {
            for (const auto& mdef : carDef->measurementDefs) {
                track::TrainOffset o;
                snprintf(o.name, sizeof(o.name), "Car %d: %s", (int)carIdx + 1, mdef.name.c_str());
                float zOffset = halfLen - (float)(currentCarDistance + mdef.localOffset.z);
                o.offset = glm::vec3(mdef.localOffset.x, mdef.localOffset.y, zOffset);

                float hue = (float)pointCounter / (float)totalMPoints;
                ImVec4 rgb;
                ImGui::ColorConvertHSVtoRGB(hue, 1.0f, 1.0f, rgb.x, rgb.y, rgb.z);
                o.color = glm::vec3(rgb.x, rgb.y, rgb.z);
                pointCounter++;

                parentTrack->trainOffsets.push_back(o);
            }
        }

        // Instantiate CPOINT camera points
        for (const auto& cdef : carDef->cameraDefs) {
            TrainCamera c;
            snprintf(c.name, sizeof(c.name), "Car %d: %s", (int)carIdx + 1, cdef.name.c_str());
            float zOffset = halfLen - (float)(currentCarDistance + cdef.localOffset.z);
            c.offset = glm::vec3(cdef.localOffset.x, cdef.localOffset.y, zOffset);
            cameras.push_back(c);
        }

        currentCarDistance += carDef->length;
    }

    if (boundCameraIndex >= (int)cameras.size()) {
        boundCameraIndex = -1;
    }

    if (parentTrack) {
        parentTrack->hasChanged = true;
        parentTrack->graphChanged = true;
    }
}

bool Train::load(const std::string& newFilePath, track* parentTrack) {
    std::string content;
    if (!VFS::get().readFileText(newFilePath, content)) {
        LOG_WARN("VFS: Failed to open train configuration '%s'", newFilePath.c_str());
        return false;
    }

    clear();
    filepath = newFilePath;

    std::string baseDir = std::filesystem::path(filepath).parent_path().string();
    if (!baseDir.empty())
        baseDir += '/';

    std::stringstream in(content);

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#')
            continue;
        std::stringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "AUTHOR") {
            std::string rem;
            std::getline(ss, rem);
            size_t q1 = rem.find('\"');
            size_t q2 = rem.find_last_of('\"');
            if (q1 != std::string::npos && q2 != std::string::npos && q1 != q2)
                author = rem.substr(q1 + 1, q2 - q1 - 1);
            else {
                std::stringstream rss(rem);
                rss >> author;
            }
        } else if (tag == "MANUFACTURER") {
            std::string rem;
            std::getline(ss, rem);
            size_t q1 = rem.find('\"');
            size_t q2 = rem.find_last_of('\"');
            if (q1 != std::string::npos && q2 != std::string::npos && q1 != q2)
                manufacturer = rem.substr(q1 + 1, q2 - q1 - 1);
            else {
                std::stringstream rss(rem);
                rss >> manufacturer;
            }
        } else if (tag == "TYPE" || tag == "COASTER_TYPE") {
            std::string rem;
            std::getline(ss, rem);
            size_t q1 = rem.find('\"');
            size_t q2 = rem.find_last_of('\"');
            if (q1 != std::string::npos && q2 != std::string::npos && q1 != q2)
                type = rem.substr(q1 + 1, q2 - q1 - 1);
            else {
                std::stringstream rss(rem);
                rss >> type;
            }
            if (type == "Wooden*")
                type = "Wooden";
        } else if (tag == "ASSET") {
            std::string id;
            ss >> id;
            std::string remaining;
            std::getline(ss, remaining);
            size_t firstQuote = remaining.find('\"');
            size_t lastQuote = remaining.find_last_of('\"');
            std::string filename;
            std::string extraParams;
            if (firstQuote != std::string::npos && lastQuote != std::string::npos && firstQuote != lastQuote) {
                filename = remaining.substr(firstQuote + 1, lastQuote - firstQuote - 1);
                extraParams = remaining.substr(lastQuote + 1);
            } else {
                std::stringstream rss(remaining);
                rss >> filename;
                std::getline(rss, extraParams);
            }
            if (!id.empty() && !filename.empty()) {
                TrainAssetDef def;
                def.id = id;
                def.glbPath = baseDir + filename;

                std::stringstream ess(extraParams);
                float sx = 1.0f, sy = 1.0f, sz = 1.0f;
                if (ess >> sx >> sy >> sz) {
                    def.scale = glm::vec3(sx, sy, sz);
                    float ox = 0.0f, oy = 0.0f, oz = 0.0f;
                    if (ess >> ox >> oy >> oz) {
                        def.offset = glm::vec3(ox, oy, oz);
                    }
                }

                def.model = new CustomTrack();
                if (!def.model->load(def.glbPath)) {
                    delete def.model;
                    def.model = nullptr;
                }
                assets.push_back(def);
            }
        } else if (tag == "CAR_TYPE") {
            TrainCarDef def;
            ss >> def.id >> def.assetId;
            def.length = 0.0f;
            def.bodyOffsetZ = 0.0f;
            carDefs.push_back(def);
        } else if (tag == "WHEEL") {
            std::string carId, wheelAssetId;
            ss >> carId >> wheelAssetId;
            for (auto& cdef : carDefs) {
                if (cdef.id == carId) {
                    cdef.wheels.push_back({wheelAssetId, 0.0f});
                    break;
                }
            }
        } else if (tag == "LAYOUT") {
            std::string carId;
            while (ss >> carId) {
                if (carId.front() == '(' && carId.back() == ')') {
                    std::string inner = carId.substr(1, carId.size() - 2);
                    repeatingCarId = inner;
                    baseLayout.push_back(inner);
                } else {
                    baseLayout.push_back(carId);
                }
            }
        }
    }

    if (repeatingCarId.empty() && baseLayout.size() >= 3) {
        repeatingCarId = baseLayout[1];
    }

    // Auto-detect kinematics and empties strictly from GLB models
    for (const auto& assetDef : assets) {
        GlbModelData modelData;
        if (readGlb(assetDef.glbPath, modelData)) {
            for (auto& carDef : carDefs) {
                if (carDef.assetId == assetDef.id) {
                    carDef.measurementDefs.clear();
                    carDef.cameraDefs.clear();
                    float bogieFrontZ = -9999.0f;
                    float bogieRearZ = -9999.0f;
                    float hitchFrontZ = -9999.0f;
                    float hitchRearZ = -9999.0f;

                    for (const auto& empty : modelData.emptyNodes) {
                        float ez = empty.worldTransform[3].z;
                        if (empty.name.find("BOGIE_FRONT") != std::string::npos || empty.name.find("FRONT_BOGIE") != std::string::npos) {
                            bogieFrontZ = ez;
                        } else if (empty.name.find("BOGIE_REAR") != std::string::npos || empty.name.find("REAR_BOGIE") != std::string::npos) {
                            bogieRearZ = ez;
                        } else if (empty.name.find("HITCH_FRONT") != std::string::npos || empty.name.find("FRONT_HITCH") != std::string::npos) {
                            hitchFrontZ = ez;
                        } else if (empty.name.find("HITCH_REAR") != std::string::npos || empty.name.find("REAR_HITCH") != std::string::npos) {
                            hitchRearZ = ez;
                        }
                    }

                    float frontZ = 0.0f;
                    if (bogieFrontZ > -9000.0f) {
                        frontZ = bogieFrontZ;
                    } else if (hitchFrontZ > -9000.0f) {
                        frontZ = hitchFrontZ;
                    }

                    carDef.bodyOffsetZ = -frontZ * assetDef.scale.z;

                    if (hitchRearZ > -9000.0f) {
                        carDef.length = std::fabs(hitchRearZ - frontZ) * assetDef.scale.z;
                    } else if (bogieRearZ > -9000.0f) {
                        carDef.length = std::fabs(bogieRearZ - frontZ) * assetDef.scale.z;
                    }

                    for (size_t wi = 0; wi < carDef.wheels.size(); ++wi) {
                        auto& w = carDef.wheels[wi];
                        if (w.wheelAssetId.find("front") != std::string::npos || (carDef.wheels.size() > 1 && wi == 0)) {
                            w.localZ = (bogieFrontZ > -9000.0f) ? std::fabs(bogieFrontZ - frontZ) * assetDef.scale.z : 0.0f;
                        } else {
                            if (bogieRearZ > -9000.0f) {
                                w.localZ = std::fabs(bogieRearZ - frontZ) * assetDef.scale.z;
                            } else if (hitchRearZ > -9000.0f) {
                                w.localZ = std::fabs(hitchRearZ - frontZ) * assetDef.scale.z;
                            } else {
                                w.localZ = 0.0f;
                            }
                        }
                    }

                    // Extract measurement points (MPOINT) and camera points (CPOINT)
                    for (const auto& empty : modelData.emptyNodes) {
                        if (empty.name.empty())
                            continue;

                        std::string upperName = empty.name;
                        for (char& ch : upperName)
                            ch = (char)std::toupper((unsigned char)ch);

                        float ez = empty.worldTransform[3].z;
                        glm::vec4 tpos = empty.worldTransform[3];
                        float distBehindFront = std::fabs(ez - frontZ) * assetDef.scale.z;

                        if (upperName.find("MPOINT") != std::string::npos) {
                            TrainMeasurementDef mdef;
                            std::string cleanName = empty.name;
                            size_t pos = upperName.find("MPOINT");
                            size_t start = pos + 6;
                            if (start < cleanName.size() && (cleanName[start] == '_' || cleanName[start] == '-')) {
                                start++;
                            }
                            if (start < cleanName.size()) {
                                cleanName = cleanName.substr(start);
                            }
                            mdef.name = cleanName;
                            mdef.localOffset = glm::vec3(tpos.x * assetDef.scale.x, tpos.y * assetDef.scale.y, distBehindFront);
                            carDef.measurementDefs.push_back(mdef);
                        }

                        if (upperName.find("CPOINT") != std::string::npos) {
                            TrainCameraDef cdef;
                            std::string cleanName = empty.name;
                            size_t pos = upperName.find("CPOINT");
                            size_t start = pos + 6;
                            if (start < cleanName.size() && (cleanName[start] == '_' || cleanName[start] == '-')) {
                                start++;
                            }
                            if (start < cleanName.size()) {
                                cleanName = cleanName.substr(start);
                            }
                            cdef.name = cleanName;
                            cdef.localOffset = glm::vec3(tpos.x * assetDef.scale.x, tpos.y * assetDef.scale.y, distBehindFront);
                            carDef.cameraDefs.push_back(cdef);
                        }
                    }
                }
            }
        }
    }

    // Resolve any car lengths that had no rear hitch (e.g. back cars)
    for (auto& carDef : carDefs) {
        if (carDef.length <= 0.0f) {
            for (const auto& other : carDefs) {
                if (other.length > 0.0f) {
                    carDef.length = other.length;
                    break;
                }
            }
        }
    }

    rebuildLayout(parentTrack);

    if (parentTrack) {
        parentTrack->requestUpdateTrack(0, 0);
    }
    return true;
}
