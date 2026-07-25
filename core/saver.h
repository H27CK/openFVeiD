/*
#    Copyright (C) 2026 Veia <h27ck@proton.me>
*/

#ifndef SAVER_H
#define SAVER_H

#include "track.h"
#include <string>
#include <vector>

class trackHandler;

class saver {
public:
    saver(const std::string& fileName, std::vector<trackHandler*>& _trackList);
    std::string doSave();
    std::string doLoad(bool importOnly = false);

    // Stream-based for Undo system
    void doSaveToStream(std::ostream& fout);
    void doLoadFromStream(std::istream& fin);

    // Chunk-based helpers
    void saveEnvironmentChunk(std::ostream& out);
    void loadEnvironmentChunk(std::istream& in, uint8_t version, uint32_t length);
    void saveGlbsChunk(std::ostream& out);
    void loadGlbsChunk(std::istream& in, uint8_t version, uint32_t length);
    void saveTracksChunk(std::ostream& out);
    void loadTracksChunk(std::istream& in, uint8_t version, uint32_t length);

    std::string sFileName;
    std::vector<trackHandler*>& trackList;
};

#endif // SAVER_H
