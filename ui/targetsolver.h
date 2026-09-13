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

#ifndef TARGETSOLVER_H
#define TARGETSOLVER_H

#include <string>

class trackHandler;
class subfunc;
class Application;

enum class TargetVariable {
    Roll = 0,
    Pitch = 1,
    Yaw = 2,
    Speed = 3,
    RollRate = 4,
    PitchRate = 5,
    YawRate = 6
};

enum class AdjustParameter {
    Length = 0,
    Amplitude = 1
};

class TargetSolver {
public:
    static bool Solve(trackHandler* hTrack, subfunc* sf, Application* app,
                      TargetVariable targetVar, double targetValue, AdjustParameter adjustParam,
                      int maxIterations, double tolerance,
                      std::string& outMessage);
};

#endif // TARGETSOLVER_H
