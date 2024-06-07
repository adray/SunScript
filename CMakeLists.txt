# CMakeLists to build the Sun compilier executable

cmake_minimum_required (VERSION 3.8)

set (SUN_SOURCES
    "SunScript.cpp"
    "Sun.cpp"
    "SunScriptDemo.cpp"
    "SunScript.h"
    "Sun.h"
    "SunScriptDemo.h"
)

project ("Sun")

add_compile_definitions(_SUN_EXECUTABLE_)

# Add source to this project's executable.
add_executable(Sun ${SUN_SOURCES})

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET Sun PROPERTY CXX_STANDARD 20)
endif()

