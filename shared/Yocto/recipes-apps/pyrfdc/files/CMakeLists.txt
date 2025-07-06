# Check cmake version
cmake_minimum_required(VERSION 3.15)
include(InstallRequiredSystemLibraries)

# Project name
project(PyRFdc)

# C/C++
enable_language(CXX)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -Wno-deprecated")
add_definitions(-D__STDC_FORMAT_MACROS)

# Set the path to RogueConfig.cmake
set(CMAKE_PREFIX_PATH "${CMAKE_SYSROOT}/usr/lib")

# Find dependencies
find_package(Rogue REQUIRED)
find_package(Python3 COMPONENTS Development REQUIRED)

# Include directories
include_directories(${CMAKE_SYSROOT}/usr/include)
include_directories(${Python3_INCLUDE_DIRS})

# Source files
add_library(PyRFdc MODULE PyRFdc.cpp)

# Link against Rogue, rfdc, and metal
target_link_libraries(PyRFdc ${Boost_LIBRARIES} ${Python3_LIBRARIES} rogue-core rfdc metal)

# Set output properties for the Python module
set_target_properties(PyRFdc PROPERTIES PREFIX "" SUFFIX ".so")

# Add linker flags explicitly
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lrfdc -lmetal")

# Install the module into the system's Python site-packages directory
install(TARGETS PyRFdc DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/python${PYTHON_VERSION_MAJOR}.${PYTHON_VERSION_MINOR}/site-packages/)
