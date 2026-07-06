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

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: bin2c <output.cpp> <asset path...>\n");
        return 1;
    }

    std::ofstream output(argv[1]);
    if (!output) {
        std::fprintf(stderr, "bin2c: cannot write %s\n", argv[1]);
        return 1;
    }

    output << "#include \"core/assets.h\"\n";
    output << "#include <unordered_map>\n\n";

    std::vector<std::string> assetPaths;
    for (int argumentIndex = 2; argumentIndex < argc; ++argumentIndex) {
        const std::string assetPath = argv[argumentIndex];
        std::ifstream input(assetPath, std::ios::binary);
        if (!input) {
            std::fprintf(stderr, "bin2c: cannot open %s\n", assetPath.c_str());
            return 1;
        }
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                         std::istreambuf_iterator<char>());

        const size_t assetIndex = assetPaths.size();
        output << "static const unsigned char assetBytes" << assetIndex << "[] = {";
        for (size_t byteIndex = 0; byteIndex < bytes.size(); ++byteIndex) {
            if (byteIndex % 20 == 0) output << "\n    ";
            output << static_cast<unsigned>(bytes[byteIndex]) << ",";
        }
        if (bytes.empty()) output << "0";
        output << "\n};\n";
        output << "static const size_t assetSize" << assetIndex << " = " << bytes.size() << ";\n\n";
        assetPaths.push_back(assetPath);
    }

    output << "static const std::unordered_map<std::string, AssetData> embeddedAssets = {\n";
    for (size_t assetIndex = 0; assetIndex < assetPaths.size(); ++assetIndex) {
        output << "    {\"" << assetPaths[assetIndex] << "\", {assetBytes" << assetIndex
               << ", assetSize" << assetIndex << "}},\n";
    }
    output << "};\n\n";
    output << "const AssetData* getEmbeddedAsset(const std::string& path) {\n";
    output << "    auto found = embeddedAssets.find(path);\n";
    output << "    return found == embeddedAssets.end() ? nullptr : &found->second;\n";
    output << "}\n";
    return 0;
}
