/*
#    Copyright (C) 2026 Veia <h27ck@proton.me>
*/

#ifndef SECNLCSV_H
#define SECNLCSV_H

#include "section.h"
#include "track.h"
#include <vector>
#include <string>

class secnlcsv : public section {
public:
    secnlcsv(track* getParent, mnode* first);
    virtual int updateSection(int node = 0);
    virtual void saveSection(std::ostream& file);
    virtual void loadSection(std::istream& file);
    virtual double getMaxArgument();
    virtual bool isLockable(func* _func);
    virtual bool isInFunction(int index, subfunc* func);
    void loadTrack(std::string filename);
    void applyFiltering();

    int skipPoints;
    int interpolation; // 0 = Linear, 1 = Cubic

private:
    std::vector<mnode> csvNodes;
    std::vector<mnode> importedNodes;
    void initDistances();
    mnode getNodeAtDistance(float distance);
};

#endif // SECNLCSV_H
