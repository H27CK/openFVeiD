/*
#    Copyright (C) 2026 Veia <h27ck@proton.me>
*/

#include "saver.h"
#include "exportfuncs.h"
#include "trackhandler.h"
#include "dummies.h"
#include "common.h"
#include "logger.h"
#include <fstream>
#include <iostream>
#include <algorithm>
#include <map>
#include <sstream>

using namespace std;

saver::saver(const std::string& fileName, std::vector<trackHandler*>& _trackList)
    : sFileName(fileName), trackList(_trackList) {}

std::string saver::doSave() {
    LOG_INFO("Saving project to: %s", sFileName.c_str());
    fstream fout(sFileName.c_str(), ios::out | ios::binary);
    if (!fout) {
        return std::string("Error: Cannot open file for writing");
    }

    doSaveToStream(fout);
    fout.close();

    return "Project saved successfully!";
}

void saver::saveEnvironmentChunk(std::ostream& out) {
    float envGrdSize = gloParent->projectGrdTexSize;
    writeBytes(&out, (const char*)&envGrdSize, sizeof(float));

    int texLen = gloParent->projectGroundTex.length();
    writeBytes(&out, (const char*)&texLen, sizeof(int));
    if (texLen > 0) {
        out.write(gloParent->projectGroundTex.data(), texLen);
    }
}

void saver::saveGlbsChunk(std::ostream& out) {
    int numGlbs = gloParent->projectGlbs.size();
    writeBytes(&out, (const char*)&numGlbs, sizeof(int));
    for (size_t i = 0; i < gloParent->projectGlbs.size(); ++i) {
        int pathLen = gloParent->projectGlbs[i].path.length();
        writeBytes(&out, (const char*)&pathLen, sizeof(int));
        if (pathLen > 0) {
            out.write(gloParent->projectGlbs[i].path.data(), pathLen);
        }
        writeBytes(&out, (const char*)&gloParent->projectGlbs[i].visible, sizeof(bool));
    }
}

void saver::saveTracksChunk(std::ostream& out) {
    for (size_t i = 0; i < trackList.size(); ++i) {
        std::stringstream trkStream;
        trackList[i]->trackData->saveTrackChunk(trkStream);
        std::string trkData = trkStream.str();
        writeChunkHeader(out, "TRK ", 1, trkData.length());
        out.write(trkData.data(), trkData.length());
    }
}

void saver::doSaveToStream(std::ostream& fout) {
    fout << "FVD";
    fout << "v1.00"; // Write the new chunk-based version tag

    // Map to hold in-memory streams for alphabetical sorting by tag
    std::map<std::string, std::string> chunks;

    // 1. Prepare ENVI chunk
    std::stringstream enviStream;
    saveEnvironmentChunk(enviStream);
    chunks["ENVI"] = enviStream.str();

    // 2. Prepare TRKS chunk
    std::stringstream trksStream;
    saveTracksChunk(trksStream);
    chunks["TRKS"] = trksStream.str();

    // 3. Prepare GLBS chunk
    std::stringstream glbsStream;
    saveGlbsChunk(glbsStream);
    chunks["GLBS"] = glbsStream.str();

    // Write all prepared chunks sorted alphabetically by tag
    for (const auto& pair : chunks) {
        if (!pair.second.empty() || pair.first == "TRKS") { // Ensure TRKS is always saved
            writeChunkHeader(fout, pair.first.c_str(), 1, pair.second.length());
            fout.write(pair.second.data(), pair.second.length());
        }
    }
}

void saver::loadEnvironmentChunk(std::istream& fin, uint8_t version, uint32_t length) {
    float envGrdSize = std::max(1.0f, readFloat(&fin));
    gloParent->projectGrdTexSize = envGrdSize;

    int texLen = 0;
    readBytes(&fin, &texLen, sizeof(int));
    if (texLen > 0) {
        char* texBuf = new char[texLen + 1];
        fin.read(texBuf, texLen);
        texBuf[texLen] = '\0';
        std::string texPath(texBuf);
        delete[] texBuf;
        gloParent->projectGroundTex = common::normalizeAssetPath(texPath);
    }
}

void saver::loadGlbsChunk(std::istream& fin, uint8_t version, uint32_t length) {
    gloParent->projectGlbs.clear();
    int size = readInt(&fin);
    for (int i = 0; i < size; ++i) {
        DummyGlobal::GlbSettings glb;
        int pLen = 0;
        readBytes(&fin, &pLen, sizeof(int));
        if (pLen > 0) {
            char* pBuf = new char[pLen + 1];
            fin.read(pBuf, pLen);
            pBuf[pLen] = '\0';
            std::string pPath(pBuf);
            delete[] pBuf;
            glb.path = common::normalizeAssetPath(pPath);
        }
        readBytes(&fin, &glb.visible, sizeof(bool));
        gloParent->projectGlbs.push_back(glb);
    }
}

void saver::loadTracksChunk(std::istream& fin, uint8_t version, uint32_t length) {
    std::streampos endPos = fin.tellg() + (std::streamoff)length;
    int trackIdx = 1;
    while (fin.tellg() < endPos) {
        ChunkHeader header = readChunkHeader(fin);
        std::string tag(header.tag, 4);
        std::streampos chunkEnd = fin.tellg() + (std::streamoff)header.length;

        if (tag == "TRK ") {
            trackHandler* newTrack = new trackHandler("Loaded Track", trackIdx++);
            newTrack->trackData->loadTrackChunk(fin, header.version, header.length);
            newTrack->trackData->requestUpdateTrack(0, 0);
            trackList.push_back(newTrack);
        } else {
            LOG_INFO("Skipping unknown chunk inside TRKS: %s", tag.c_str());
        }
        fin.seekg(chunkEnd, ios::beg);
    }
}

std::string saver::doLoad(bool importOnly) {
    LOG_INFO("%s project from: %s", importOnly ? "Importing track(s) from" : "Loading project from:", sFileName.c_str());
    fstream fin(sFileName.c_str(), ios::in | ios::binary);
    if (!fin) {
        return std::string("Error: Cannot open file for reading");
    }

    if (importOnly) {
        std::string temp = readString(&fin, 3);
        if (temp != "FVD") {
            fin.close();
            return "Error while loading: not an FVD file!";
        }
        temp = readString(&fin, 5); // version

        if (temp == "v1.00") {
            while (fin.peek() != EOF) {
                ChunkHeader header = readChunkHeader(fin);
                if (!fin)
                    break;
                std::string tag(header.tag, 4);
                std::streampos chunkEnd = fin.tellg() + (std::streamoff)header.length;

                if (tag == "TRKS") {
                    loadTracksChunk(fin, header.version, header.length);
                }
                fin.seekg(chunkEnd, ios::beg);
            }
        } else {
            fin.close();
            return "Error while loading: Unsupported version!";
        }
    } else {
        doLoadFromStream(fin);
    }

    fin.close();
    return "Project loaded successfully!";
}

void saver::doLoadFromStream(std::istream& fin) {
    std::string temp = readString(&fin, 3);
    if (temp != "FVD")
        return;
    temp = readString(&fin, 5);

    if (temp == "v1.00") {
        // Clear current track list
        for (auto t : trackList) {
            delete t;
        }
        trackList.clear();
        gloParent->resetEnvironment();

        while (fin.peek() != EOF) {
            ChunkHeader header = readChunkHeader(fin);
            if (!fin)
                break;

            std::string tag(header.tag, 4);
            std::streampos chunkStart = fin.tellg();
            std::streampos chunkEnd = chunkStart + (std::streamoff)header.length;

            if (tag == "ENVI") {
                loadEnvironmentChunk(fin, header.version, header.length);
            } else if (tag == "TRKS") {
                loadTracksChunk(fin, header.version, header.length);
            } else if (tag == "GLBS") {
                loadGlbsChunk(fin, header.version, header.length);
            } else {
                LOG_INFO("Skipping unknown top-level chunk: %s", tag.c_str());
            }
            fin.seekg(chunkEnd, ios::beg);
        }
    } else {
        LOG_INFO("Error while loading: Unsupported version %s", temp.c_str());
    }
}
