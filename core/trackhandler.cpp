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

#include "trackhandler.h"
#include "track.h"
#include "trackmesh.h"
#include "dummies.h"
#include "logger.h"
#include <iostream>

trackHandler::trackHandler(std::string _name, int _id) {
    LOG_INFO("Creating track: %s (ID: %d)", _name.c_str(), _id);
    id = _id;
    this->trackData = new track(this, glm::vec3(0.f, 5.f, 0.f), 0.0);
    trackData->name = _name;

    graphWidgetItem = nullptr;
    trackWidgetItem = nullptr;

    tabId = -1;

    trackColors[0] = glm::vec3(74 / 255.f, 95 / 255.f, 230 / 255.f); // Default (#4a5fe6)
    trackColors[1] = glm::vec3(229 / 255.f, 83 / 255.f, 83 / 255.f); // Section (#e55353)
    trackColors[2] = glm::vec3(61 / 255.f, 231 / 255.f, 61 / 255.f); // Transition (#3de73d)
    trackColors[3] = glm::vec3(0.9f, 0.9f, 0.4f);                    // Heartline (Default: Yellow)

    mMesh = new trackMesh(trackData);
}

trackHandler::trackHandler(std::string _name, int _id, bool isReference) {
    LOG_INFO("Creating reference track: %s (ID: %d)", _name.c_str(), _id);
    id = _id;
    if (isReference) {
        this->trackData = new reftrack(this, glm::vec3(0.f, 5.f, 0.f), 0.0);
    } else {
        this->trackData = new track(this, glm::vec3(0.f, 5.f, 0.f), 0.0);
    }
    trackData->name = _name;

    graphWidgetItem = nullptr;
    trackWidgetItem = nullptr;

    tabId = -1;

    trackColors[0] = glm::vec3(74 / 255.f, 95 / 255.f, 230 / 255.f); // Default (#4a5fe6)
    trackColors[1] = glm::vec3(229 / 255.f, 83 / 255.f, 83 / 255.f); // Section (#e55353)
    trackColors[2] = glm::vec3(61 / 255.f, 231 / 255.f, 61 / 255.f); // Transition (#3de73d)
    trackColors[3] = glm::vec3(0.9f, 0.9f, 0.4f);                    // Heartline (Default: Yellow)

    mMesh = new trackMesh(trackData);
}

trackHandler::~trackHandler() {
    // delete trackWidgetItem;
    // delete graphWidgetItem;
    delete trackData;
    delete mMesh;
}

void trackHandler::changeID(int _id) {
    id = _id;
}

int trackHandler::getID() {
    return id;
}
