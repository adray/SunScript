#pragma once
#include <string>

#define OPT_NONE 0x0
#define OPT_DUMPTRACE 0x1
#define OPT_DEBUG 0x2

namespace SunScript
{
    void RunTestSuite(const std::string& path, int opts);
}
