#pragma once
#include <string>

namespace SunScript
{
    void CompileFile(const std::string& filepath, unsigned char** programData);
    void CompileFile(const std::string& filepath, unsigned char** programData, unsigned char** debugData, std::string* error);
}
