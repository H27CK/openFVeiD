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

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <iomanip>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: bin2c <output.cpp> <file1> <file2> ..." << std::endl;
        return 1;
    }

    std::string outPath = argv[1];
    std::ofstream out(outPath);
    if (!out)
        return 1;

    out << "#include \"core/assets.h\"\n";
    out << "#include <map>\n\n";

    std::vector<std::string> varNames;
    std::vector<std::string> fileNames;

    for (int i = 2; i < argc; ++i) {
        std::string filePath = argv[i];
        std::ifstream in(filePath, std::ios::binary);
        if (!in) {
            std::cerr << "Failed to open " << filePath << std::endl;
            return 1;
        }

        std::string varName = filePath;
        for (char& c : varName) {
            if (!isalnum(c))
                c = '_';
        }

        out << "static const unsigned char " << varName << "[] = {\n";
        char c;
        size_t count = 0;
        while (in.get(c)) {
            out << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)(unsigned char)c << ",";
            count++;
            if (count % 16 == 0)
                out << "\n";
        }
        out << "\n};\n";
        out << "static const size_t " << varName << "_size = " << std::dec << count << ";\n\n";

        varNames.push_back(varName);
        fileNames.push_back(filePath);
    }

    out << "static const std::map<std::string, AssetData> g_assets = {\n";
    for (size_t i = 0; i < varNames.size(); ++i) {
        std::string normalizedPath = fileNames[i];
        for (char& c : normalizedPath)
            if (c == '\\')
                c = '/';
        out << "    {\"" << normalizedPath << "\", {" << varNames[i] << ", " << varNames[i] << "_size}},\n";
    }
    out << "};\n\n";

    out << "const AssetData* getEmbeddedAsset(const std::string& path) {\n";
    out << "    auto it = g_assets.find(path);\n";
    out << "    if (it != g_assets.end()) return &it->second;\n";
    out << "    return nullptr;\n";
    out << "}\n";

    return 0;
}