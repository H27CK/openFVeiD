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

#include "transitionview.h"
#include "trackhandler.h"
#include "track.h"
#include "section.h"
#include "function.h"
#include "subfunction.h"
#include "dummies.h"
#include "core/application.h"
#include "core/globalundohandler.h"
#include "common.h"
#include <iostream>
#include <cmath>

TransitionView::TransitionView() {}
TransitionView::~TransitionView() {}

#define BEGIN_PROP_TABLE(id)                                                                        \
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingFixedFit)) { \
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 120.0f);                 \
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

#define PROP_ROW(label, ...)           \
    ImGui::TableNextRow();             \
    ImGui::TableNextColumn();          \
    ImGui::AlignTextToFramePadding();  \
    ImGui::Text("%s", label);          \
    ImGui::TableNextColumn();          \
    ImGui::SetNextItemWidth(-FLT_MIN); \
    __VA_ARGS__

#define END_PROP_TABLE() \
    ImGui::EndTable();   \
    }

void TransitionView::render(trackHandler* hTrack, subfunc* sf, Application* app) {
    bool styleIsLocked = false;
    if (styleIsLocked) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.0f, 1.0f), "Track editing is disabled while a Custom 3D Style is active.");
        ImGui::BeginDisabled();
    }

    if (sf) {
        BEGIN_PROP_TABLE("TransitionProps")
        renderBasicProperties(hTrack, sf, app);
        renderTypeSpecificProperties(hTrack, sf, app);
        renderTimewarpProperties(hTrack, sf, app);
        END_PROP_TABLE()

        ImGui::Separator();
        renderActions(hTrack, sf, app);
        ImGui::Separator();
    } else {
        ImGui::TextDisabled("Select a transition from the list or graph to edit properties.");
        ImGui::Separator();
    }

    renderSelectionList(hTrack, sf, app);

    if (ImGui::GetCurrentContext()->ActiveIdPreviousFrame != 0 && ImGui::GetCurrentContext()->ActiveId == 0) {
        if (app) {
            app->pushUndo();
        }
    }

    if (styleIsLocked)
        ImGui::EndDisabled();
}

void TransitionView::renderSelectionList(trackHandler* hTrack, subfunc* currentSf, Application* app) {
    if (!hTrack || !hTrack->trackData || !hTrack->trackData->activeSection)
        return;

    section* sec = hTrack->trackData->activeSection;
    if (sec->type == anchor)
        return;

    auto renderFuncList = [&](const char* label, func* f) {
        if (!f)
            return;
        if (ImGui::TreeNodeEx(label)) {
            for (int i = 0; i < (int)f->funcList.size(); ++i) {
                subfunc* sf = f->funcList[i];
                bool isSelected = (sf == currentSf);
                char buf[128];

                float lenFactor = 1.0f;
                if (sf->parent->secParent->type != curved && (sf->parent->secParent->type == straight || sf->parent->secParent->bArgument != TIME) && gloParent) {
                    lenFactor = gloParent->mOptions->getLengthFactor();
                }
                double rawLength = sf->maxArgument - sf->minArgument;
                double displayLength = rawLength * lenFactor;
                std::string lenSuffix = getLengthSuffix(sf);

                const char* degreeStr = "Transition";
                switch (sf->degree) {
                case linear:
                    degreeStr = "Linear";
                    break;
                case quadratic:
                    degreeStr = "Quadratic";
                    break;
                case cubic:
                    degreeStr = "Cubic";
                    break;
                case quartic:
                    degreeStr = "Quartic";
                    break;
                case quintic:
                    degreeStr = "Quintic";
                    break;
                case sinusoidal:
                    degreeStr = "Sinusoidal";
                    break;
                case plateau:
                    degreeStr = "Plateau";
                    break;
                case tozero:
                    degreeStr = "To Zero";
                    break;
                case freeform:
                    degreeStr = "Freeform";
                    break;
                case custom_math:
                    degreeStr = "Custom";
                    break;
                }

                sprintf(buf, "%d: %s %.2f %s%s%s###sf_%p", i, degreeStr, (float)displayLength, lenSuffix.c_str(),
                        sf->locked ? " [Dynamic]" : "",
                        (rawLength <= 0) ? " (HIDDEN)" : "",
                        (void*)sf);

                if (ImGui::Selectable(buf, isSelected)) {
                    if (gloParent->selectedFunc != sf) {
                        gloParent->selectedFunc = sf;
                        hTrack->trackData->requestUpdateTrack(0, 0);
                    }
                }
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::TreePop();
        }
    };

    renderFuncList("Roll Transitions", sec->rollFunc);
    if (sec->type == forced || sec->type == geometric || sec->type == geometricriderlocal) {
        bool isGeoLike = (sec->type == geometric || sec->type == geometricriderlocal);
        renderFuncList(isGeoLike ? "Pitch Transitions" : "Normal Force Transitions", sec->normForce);
        renderFuncList(isGeoLike ? "Yaw Transitions" : "Lateral Force Transitions", sec->latForce);
    }
    ImGui::Separator();
}

void TransitionView::renderBasicProperties(trackHandler* hTrack, subfunc* sf, Application* app) {
    if (!sf || !sf->parent || !sf->parent->secParent)
        return;

    float lenFactor = 1.0f;
    if (sf->parent->secParent->type != curved && (sf->parent->secParent->type == straight || sf->parent->secParent->bArgument != TIME) && gloParent) {
        lenFactor = gloParent->mOptions->getLengthFactor();
    }

    float length = (sf->maxArgument - sf->minArgument) * lenFactor;
    std::string lenSuffix = getLengthSuffix(sf);

    PROP_ROW(
        "Length",
        if (ImGui::DragFloat(("##Length" + lenSuffix).c_str(), &length, 0.01f, 0.1f, 1000.0f * lenFactor, ("%.3f " + lenSuffix).c_str()) || common::ValueScroll(&length, 1.0f, 0.1f)) {
            if (length < 0.1f)
                length = 0.1f;
            sf->parent->changeLength(length / lenFactor, sf->parent->getSubfuncNumber(sf));
            hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
        })

    if (sf->parent->secParent->type == forced || sf->parent->secParent->type == geometric || sf->parent->secParent->type == geometricriderlocal) {
        bool isLocked = sf->locked;
        bool canLock = true;

        if (!isLocked) {
            if (sf->parent->lockedFunc() > -1) {
                canLock = false;
            } else if (!sf->parent->secParent->isLockable(sf->parent)) {
                canLock = false;
            }
        }

        if (!canLock)
            ImGui::BeginDisabled();
        PROP_ROW(
            "Dynamic",
            if (ImGui::Checkbox("##Dynamic", &isLocked)) {
                if (sf->parent->secParent->setLocked(sf->parent->type, sf->parent->getSubfuncNumber(sf), isLocked)) {
                    hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
                    app->pushUndo();
                }
            })
        if (!canLock) {
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("Cannot set to dynamic. Max 1 dynamic transition per graph, and max 2 dynamic graphs per section.");
            }
        }
    }

    float changeFactor = 1.0f;
    if (sf->parent->secParent->bArgument != TIME && sf->parent->type != funcNormal && sf->parent->type != funcLateral && gloParent) {
        changeFactor = gloParent->mOptions->getLengthFactor();
    }

    float change = sf->symArg / changeFactor;
    std::string chgSuffix = getChangeSuffix(sf);
    PROP_ROW(
        "Change",
        if (ImGui::DragFloat(("##Change" + chgSuffix).c_str(), &change, 0.01f, -500.0f / changeFactor, 500.0f / changeFactor, ("%.3f " + chgSuffix).c_str()) || common::ValueScroll(&change, 1.0f, 0.1f)) {
            sf->symArg = change * changeFactor;
            hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
        })

    const char* typesRoll[] = {"Linear", "Quadratic", "Cubic", "Quartic", "Quintic", "Sinusoidal", "Plateau", "ToZero", "Custom"};
    const char* typesForce[] = {"Linear", "Quadratic", "Cubic", "Quartic", "Quintic", "Sinusoidal", "Plateau", "Custom"};
    const char** types;
    int numTypes;
    int typeIdx;
    if (sf->parent->type == funcRoll) {
        types = typesRoll;
        numTypes = 9;
        if (sf->degree == custom_math) {
            typeIdx = 8;
        } else {
            typeIdx = (int)sf->degree;
        }
    } else {
        types = typesForce;
        numTypes = 8;
        if (sf->degree == custom_math) {
            typeIdx = 7;
        } else {
            typeIdx = (int)sf->degree;
        }
    }

    PROP_ROW(
        "Type",
        if (ImGui::Combo("##Type", &typeIdx, types, numTypes)) {
            eDegree targetDegree;
            if (sf->parent->type == funcRoll) {
                if (typeIdx == 8) {
                    targetDegree = custom_math;
                } else {
                    targetDegree = (eDegree)typeIdx;
                }
            } else {
                if (typeIdx == 7) {
                    targetDegree = custom_math;
                } else {
                    targetDegree = (eDegree)typeIdx;
                }
            }
            sf->changeDegree(targetDegree);
            hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
            app->pushUndo();
        })
}

void TransitionView::renderTypeSpecificProperties(trackHandler* hTrack, subfunc* sf, Application* app) {
    bool changed = false;

    if (sf->degree == quadratic) {
        const char* qTypes[] = {"Blend in", "Blend out", "Symmetric"};
        int qIdx = sf->arg1 == 0.f ? 2 : (sf->arg1 > 0 ? 0 : 1);
        PROP_ROW(
            "Quadratic Type",
            if (ImGui::Combo("##QuadType", &qIdx, qTypes, IM_ARRAYSIZE(qTypes))) {
                if (qIdx == 0)
                    sf->arg1 = 1.f;
                else if (qIdx == 1)
                    sf->arg1 = -1.f;
                else
                    sf->arg1 = 0.f;
                changed = true;
                app->pushUndo();
            })
    } else if (sf->degree == quartic) {
        const char* qTypes[] = {"Symmetric function", "Opposite direction first", "Overshoot final value"};
        int qIdx = (sf->arg1 < -1) ? 0 : (sf->arg1 < 0.5f ? 1 : 2);

        PROP_ROW(
            "Quartic Type",
            if (ImGui::Combo("##QuartType", &qIdx, qTypes, IM_ARRAYSIZE(qTypes))) {
                if (qIdx == 0)
                    sf->arg1 = -10.f;
                else if (qIdx == 1)
                    sf->arg1 = 0.0f;
                else if (qIdx == 2)
                    sf->arg1 = 1.0f;
                changed = true;
                app->pushUndo();
            })

        if (qIdx > 0) {
            float val = 0.0f;
            if (qIdx == 1)
                val = (0.5f / (0.5f - sf->arg1) - 1.0f) / 5.0f;
            if (qIdx == 2)
                val = (0.5f / (sf->arg1 - 0.5f) - 1.0f) / 5.0f;
            if (val < 0.0f)
                val = 0.0f;

            PROP_ROW(
                "Amount",
                if (ImGui::DragFloat("##Amount", &val, 0.01f, 0.0f, 10.0f) || common::ValueScroll(&val, 1.0f, 0.1f)) {
                    if (val < 0.0f)
                        val = 0.0f;
                    if (qIdx == 1)
                        sf->arg1 = 0.5f - 0.5f / (1 + 5 * val);
                    if (qIdx == 2)
                        sf->arg1 = 0.5f + 0.5f / (1 + 5 * val);
                    changed = true;
                })
        }
    } else if (sf->degree == quintic) {
        const char* qTypes[] = {"Simple Transition", "DoubleBump(1)", "DoubleBump(2)"};
        int qIdx = (sf->arg1 == 0.f) ? 0 : (sf->arg1 < 0 ? 1 : 2);

        PROP_ROW(
            "Quintic Type",
            if (ImGui::Combo("##QuintType", &qIdx, qTypes, IM_ARRAYSIZE(qTypes))) {
                if (qIdx == 0)
                    sf->arg1 = 0.f;
                else if (qIdx == 1)
                    sf->arg1 = -5.0f;
                else if (qIdx == 2)
                    sf->arg1 = 5.0f;
                changed = true;
                app->pushUndo();
            })

        if (qIdx > 0) {
            float val = std::fabs(sf->arg1);
            PROP_ROW(
                "Amount",
                if (ImGui::DragFloat("##QuintAmount", &val, 0.01f, 2.0f, 8.0f) || common::ValueScroll(&val, 1.0f, 0.1f)) {
                    if (val < 2.0f)
                        val = 2.0f;
                    if (val > 8.0f)
                        val = 8.0f;
                    if (qIdx == 1)
                        sf->arg1 = -val;
                    if (qIdx == 2)
                        sf->arg1 = val;
                    changed = true;
                })
        }
    } else if (sf->degree == plateau) {
        float val = (float)sf->arg1;
        PROP_ROW(
            "Amount",
            if (ImGui::DragFloat("##PlateauAmount", &val, 0.01f, 0.1f, 10.0f) || common::ValueScroll(&val, 1.0f, 0.1f)) {
                if (val < 0.1f)
                    val = 0.1f;
                sf->arg1 = (double)val;
                changed = true;
            })
    } else if (sf->degree == custom_math) {
        char buf[256];
        strncpy(buf, sf->customExpr.c_str(), sizeof(buf));
        buf[sizeof(buf) - 1] = '\0';
        PROP_ROW(
            "Formula",
            if (ImGui::InputText("##Formula", buf, sizeof(buf))) {
                sf->customExpr = buf;
                sf->compileExpr();
                changed = true;
                app->pushUndo();
            })
        if (!sf->compileError.empty()) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error");
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", sf->compileError.c_str());
        }
    }

    if (changed) {
        sf->parent->translateValues(sf);
        hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
    }
}

void TransitionView::renderTimewarpProperties(trackHandler* hTrack, subfunc* sf, Application* app) {
    if (sf->degree != freeform && sf->degree != tozero) {
        float centerArg = (float)sf->centerArg;
        PROP_ROW(
            "Tw Center",
            if (ImGui::DragFloat("##TwCenter", &centerArg, 0.01f, -10.0f, 10.0f) || common::ValueScroll(&centerArg, 1.0f, 0.1f)) {
                sf->centerArg = (double)centerArg;
                hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
            })
        float tensionArg = (float)sf->tensionArg;
        PROP_ROW(
            "Tw Tension",
            if (ImGui::DragFloat("##TwTension", &tensionArg, 0.01f, -10.0f, 10.0f) || common::ValueScroll(&tensionArg, 1.0f, 0.1f)) {
                sf->tensionArg = (double)tensionArg;
                hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
            })
    }
}

void TransitionView::renderActions(trackHandler* hTrack, subfunc* sf, Application* app) {
    int idx = sf->parent->getSubfuncNumber(sf);

    if (ImGui::Button("Append")) {
        sf->parent->appendSubFunction(1.0f, idx);
        gloParent->selectedFunc = sf->parent->funcList[idx + 1];
        hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
        app->pushUndo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Prepend")) {
        sf->parent->appendSubFunction(1.0f, idx - 1);
        gloParent->selectedFunc = sf->parent->funcList[idx];
        hTrack->trackData->requestUpdateTrack(sf->parent->secParent, 0);
        app->pushUndo();
    }
    ImGui::SameLine();
    bool canRemove = (sf->parent->funcList.size() > 1);
    if (!canRemove)
        ImGui::BeginDisabled();
    if (ImGui::Button("Remove")) {
        func* fParent = sf->parent;
        fParent->removeSubFunction(idx);
        if (!fParent->funcList.empty()) {
            int newIdx = (idx > 0) ? idx - 1 : 0;
            if (newIdx < (int)fParent->funcList.size()) {
                gloParent->selectedFunc = fParent->funcList[newIdx];
            } else {
                gloParent->selectedFunc = nullptr;
            }
        } else {
            gloParent->selectedFunc = nullptr;
        }
        hTrack->trackData->requestUpdateTrack(fParent->secParent, 0);
        app->pushUndo();
    }
    if (!canRemove)
        ImGui::EndDisabled();
}
std::string TransitionView::getLengthSuffix(subfunc* sf) {
    if (!sf || !sf->parent || !sf->parent->secParent)
        return "";

    if (sf->parent->secParent->type == straight) {
        if (gloParent)
            return gloParent->mOptions->getLengthString();
    }
    if (sf->parent->secParent->type == curved) {
        return "deg";
    }
    if (sf->parent->secParent->bArgument == TIME)
        return "s";
    if (gloParent)
        return gloParent->mOptions->getLengthString();
    return "m";
}

std::string TransitionView::getChangeSuffix(subfunc* sf) {
    if (!sf || !sf->parent || !sf->parent->secParent)
        return "";
    switch (sf->parent->type) {
    case funcNormal:
    case funcLateral:
        return "g";
    case funcRoll:
    case funcPitch:
    case funcYaw: {
        if (sf->parent->secParent->type == straight) {
            return "deg/s";
        }
        if (sf->parent->secParent->type == curved) {
            return "deg/s";
        }
        if (sf->parent->secParent->bArgument == TIME)
            return "deg/s";
        if (gloParent)
            return "deg/" + gloParent->mOptions->getLengthString();
        return "deg/m";
    }
    default:
        return "";
    }
}
