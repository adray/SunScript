# CMakeLists to build the Sun compilier executable

cmake_minimum_required (VERSION 3.8)

set (SUN_SOURCES
    "SunScript.cpp"
    "Sun.cpp"
    "SunScriptDemo.cpp"
    "SunJIT.cpp"
    "SunScript.h"
    "Sun.h"
    "SunScriptDemo.h"
    "SunJIT.h"
    "Tests/SunTest.h"
    "Tests/SunTest.cpp"
)

set (SUN_TESTS
    "Tests/Arithmetic.txt"
    "Tests/BranchTest.txt"
    "Tests/Coroutine.txt"
    "Tests/Factorial.txt"
    "Tests/ForLoop.txt"
    "Tests/Guard.txt"
    "Tests/LoopTest.txt"
    "Tests/NestedLoop.txt"
    "Tests/Optimize.txt"
    "Tests/Phi.txt"
    "Tests/Spill.txt"
)

project ("Sun")

add_compile_definitions(_SUN_EXECUTABLE_)
#add_compile_definitions(USE_SUN_FLOAT)

# Add source to this project's executable.
add_executable(Sun ${SUN_SOURCES} ${SUN_TESTS})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Sun PROPERTY CXX_STANDARD 20)
endif()

