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

#ifndef TRAIN_H
#define TRAIN_H

#include <glm/glm.hpp>
#include <string>
#include <vector>

class CustomTrack;
class track;

struct TrainWheelDef {
    std::string wheelAssetId;
    float localZ = 0.0f;
};

struct TrainMeasurementDef {
    std::string name;
    glm::vec3 localOffset = glm::vec3(0.0f);
};

struct TrainCameraDef {
    std::string name;
    glm::vec3 localOffset = glm::vec3(0.0f);
};

struct TrainCarDef {
    std::string id;
    float length = 1.05f;
    std::string assetId;
    float bodyOffsetZ = 0.0f;
    std::vector<TrainWheelDef> wheels;
    std::vector<TrainMeasurementDef> measurementDefs;
    std::vector<TrainCameraDef> cameraDefs;
};

struct TrainAssetDef {
    std::string id;
    std::string glbPath;
    glm::vec3 scale = glm::vec3(1.0f);
    glm::vec3 offset = glm::vec3(0.0f);
    CustomTrack* model = nullptr;
};

struct TrainCamera {
    char name[64] = {0};
    glm::vec3 offset = glm::vec3(0.0f);
};

class Train {
public:
    Train();
    ~Train();

    bool load(const std::string& filepath, track* parentTrack = nullptr);
    void clear();
    void setMiddleCarCount(int count, track* parentTrack = nullptr);

    float getTotalLength() const;
    bool isLoaded() const {
        return !filepath.empty();
    }

    const std::string& getFilePath() const {
        return filepath;
    }
    const std::vector<TrainAssetDef>& getAssets() const {
        return assets;
    }
    const std::vector<TrainCarDef>& getCarDefs() const {
        return carDefs;
    }
    const std::vector<std::string>& getLayout() const {
        return layout;
    }
    const std::vector<TrainCamera>& getCameras() const {
        return cameras;
    }

    const TrainCarDef* findCarDef(const std::string& carId) const;

    int middleCarCount = 3;
    int boundCameraIndex = -1;

    std::string author;
    std::string manufacturer;
    std::string type;

private:
    void rebuildLayout(track* parentTrack);
    void rebuildPoints(track* parentTrack);

    std::string filepath;
    std::vector<TrainAssetDef> assets;
    std::vector<TrainCarDef> carDefs;
    std::vector<std::string> baseLayout;
    std::vector<std::string> layout;
    std::string repeatingCarId;
    std::vector<TrainCamera> cameras;
};

#endif // TRAIN_H
