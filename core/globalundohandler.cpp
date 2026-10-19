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

#include "core/dummies.h"
#include "application.h"
#include "saver.h"
#include "logger.h"
#include "viewport.h"
#include "globalundohandler.h"
#include <sstream>

GlobalUndoHandler::GlobalUndoHandler(Application* app, int stackSize) {
    mApp = app;
    maxStackSize = stackSize;
    stackIndex = -1;
    busy = false;
}

GlobalUndoHandler::~GlobalUndoHandler() {
    clearActions();
}

void GlobalUndoHandler::pushSnapshot() {
    if (busy)
        return;

    std::ostringstream oss(std::ios::binary);
    saver s("", mApp->trackList);
    s.doSaveToStream(oss);

    int activeIdx = mApp->activeTrackIdx;
    oss.write((const char*)&activeIdx, sizeof(int));

    std::string newSnapshot = oss.str();

    if (stackIndex >= 0 && newSnapshot == stack[stackIndex]) {
        return;
    }

    if (stackIndex < (int)stack.size() - 1) {
        stack.erase(stack.begin() + stackIndex + 1, stack.end());
    }

    stack.push_back(std::move(newSnapshot));
    stackIndex++;

    if ((int)stack.size() > maxStackSize) {
        stack.erase(stack.begin());
        stackIndex--;
    }

    LOG_INFO("Global project snapshot pushed. Stack size: %d", (int)stack.size());
}

void GlobalUndoHandler::doUndo() {
    if (!canUndo() || busy)
        return;

    busy = true;
    stackIndex--;

    std::string data = stack[stackIndex];
    std::istringstream iss(data, std::ios::binary);

    saver s("", mApp->trackList);
    s.doLoadFromStream(iss);

    iss.read((char*)&mApp->activeTrackIdx, sizeof(int));

    if (gViewport)
        gViewport->markSceneDirty();

    busy = false;
    LOG_INFO("Global Undo performed. Stack index: %d", stackIndex);
}

void GlobalUndoHandler::doRedo() {
    if (!canRedo() || busy)
        return;

    busy = true;
    stackIndex++;

    std::string data = stack[stackIndex];
    std::istringstream iss(data, std::ios::binary);

    saver s("", mApp->trackList);
    s.doLoadFromStream(iss);

    iss.read((char*)&mApp->activeTrackIdx, sizeof(int));

    if (gViewport)
        gViewport->markSceneDirty();

    busy = false;
    LOG_INFO("Global Redo performed. Stack index: %d", stackIndex);
}

void GlobalUndoHandler::clearActions() {
    stack.clear();
    stackIndex = -1;
}
