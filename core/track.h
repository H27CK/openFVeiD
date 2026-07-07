#ifndef TRACK_H
#define TRACK_H

/*
#    FVD++, an advanced coaster design tool for NoLimits
#    Copyright (C) 2012-2015, Stephan "Lenny" Alt <alt.stephan@web.de>
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
#    along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "mnode.h"
#include "secbezier.h"
#include "seccurved.h"
#include "secforced.h"
#include "secgeometric.h"
#include "secgeometricriderlocal.h"
#include "secnlcsv.h"
#include "secstraight.h"
#include "sectionhandler.h"
#include <vector>
#include <string>
#include <fstream>

class sectionHandler;
class smoothHandler;
class trackHandler;

enum trackStyle {
    parametric = 0
};

#pragma pack(push, 1)
struct ChunkHeader {
    char tag[4];
    uint8_t version;
    uint32_t length;
};
#pragma pack(pop)

void writeChunkHeader(std::ostream& out, const char* tag, uint8_t version, uint32_t length);
ChunkHeader readChunkHeader(std::istream& in);

class CustomTrackStyle;

class track {
public:
    enum class ExtrusionShape { Cylindrical,
                                Box };

    struct ParametricExtrusion {
        ExtrusionShape shape = ExtrusionShape::Cylindrical;
        glm::vec2 size = glm::vec2(0.5f);          // X=Diameter/L1, Y=L2 (unused for cylindrical)
        glm::vec2 offset = glm::vec2(0.0f, -1.0f); // X=Lateral, Y=Vertical relative to heartline
    };

    struct CustomAssetInstance {
        std::string filepath;
        CustomTrackStyle* loadedModel = nullptr; // The loaded GLTF data
        float startDist = 0.0f;                  // Distance along spline to start
        float endDist = -1.0f;                   // Distance along spline to stop (-1 means end of track)
        float interval = 1.0f;                   // Spacing between instances
        glm::vec3 color = glm::vec3(0.7f);       // Asset color
        bool fullLayout = false;                 // Automatically cover entire layout
        bool toEnd = true;                       // Automatically repeat to end of track
        bool visible = true;                     // Whether the asset is visible
        bool smoothAlongSpline = true;           // Blend duplicate-vertex normals for smoother shading
    };

    track();
    track(trackHandler* _parent, glm::dvec3 startPos, double startYaw,
          double heartLine = 0.0);
    ~track();
    void removeSection(int index);
    void removeSection(section* fromSection);

    void removeSmooth(int fromNode = 0);
    void applySmooth(int fromNode = 0);

    void updateTrack(int index, int iNode);
    void updateTrack(section* fromSection, int iNode);
    void requestUpdateTrack(int index, int iNode);
    void requestUpdateTrack(section* fromSection, int iNode);
    void processPendingUpdates();
    void newSection(enum secType type, int index = -1, bool deferUpdate = false);

    void clearTrack();

    float getTotalLength() const;

    int exportTrack(std::fstream* file, double mPerNode, int fromIndex,
                    int toIndex, double fRollThresh);
    int exportTrack2(std::fstream* file, double mPerNode, int fromIndex,
                     int toIndex, double fRollThresh);
    int exportTrack3(std::fstream* file, double mPerNode, int fromIndex,
                     int toIndex, double fRollThresh);
    int exportTrack4(std::fstream* file, double mPerNode, int fromIndex,
                     int toIndex, double fRollThresh);

    void exportNL2Track(FILE* file, double mPerNode, int fromIndex, int toIndex);
    void exportNL2TrackCSV(FILE* file, double mPerNode, int fromIndex,
                           int toIndex);

    std::string saveTrack(std::ostream& file);
    std::string loadTrack(std::istream& file);

    void saveTrackChunk(std::ostream& file);
    void loadTrackChunk(std::istream& file, uint8_t version, uint32_t length);
    void loadPropChunk(std::istream& file, uint8_t version, uint32_t length);
    void loadSecsChunk(std::istream& file, uint8_t version, uint32_t length);
    void loadExtrChunk(std::istream& file, uint8_t version, uint32_t length);
    void loadAsstChunk(std::istream& file, uint8_t version, uint32_t length);
    void loadOffsChunk(std::istream& file, uint8_t version, uint32_t length);

    bool exportParametricStyle(const std::string& filepath);
    bool importParametricStyle(const std::string& filepath);

    bool exportMeasurementPoints(const std::string& filepath);
    bool importMeasurementPoints(const std::string& filepath);

    mnode* getPoint(int index);
    int getIndexFromDist(double dist);
    int getNumPoints(section* until = NULL);
    int getSectionNumber(section* _section);

    void getSecNode(int index, int* node, int* section);

    bool hasChanged;
    bool graphChanged;
    double lastUpdateTimeMs = 0.0;
    bool isAnyNodeNearGimbalLock = false;
    int drawHeartline;

    bool pendingRebuild = false;
    int pendingRebuildIndex = 0;
    int pendingRebuildNode = 0;

    int maxNormalOffsetIdx = -1;
    int minNormalOffsetIdx = -1;
    int maxLateralOffsetIdx = -1;
    int minLateralOffsetIdx = -1;
    int forceHighlightMode = 0; // 0=None, 1=Normal, 2=Lateral

    mnode* anchorNode;

    glm::dvec3 startPos;
    double startYaw;
    double startPitch;

    section* activeSection;

    double fHeart;
    double fFriction;
    double fResistance;

    bool enableForceLimits;
    double fMaxPosNormal;
    double fMaxNegNormal;
    double fMaxLateral;

    struct TrainOffset {
        char name[64] = "Measurement Point";
        glm::vec3 offset = glm::vec3(0.0f);
        bool showNormal = true;
        bool showLateral = true;
        glm::vec3 color = glm::vec3(1.0f);
    };
    std::vector<TrainOffset> trainOffsets;

    std::vector<section*> lSections;

    void* mOptions;
    std::string name;

    void* smoother;

    std::vector<smoothHandler*> smoothList;

    trackHandler* mParent;

    int smoothedUntil;
    enum trackStyle style;
    glm::vec2 povPos;

    std::string customStyleFile;

    std::vector<ParametricExtrusion> customExtrusions;
    std::vector<CustomAssetInstance> customAssets;
};

#endif // TRACK_H
