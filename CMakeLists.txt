cmake_minimum_required(VERSION 3.23 FATAL_ERROR)
project(horus DESCRIPTION "Horus" VERSION 0.3.0 LANGUAGES CXX CUDA)

# Conan
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# OpenCV
list(PREPEND CMAKE_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party/opencv/release)

# Dependencies
find_package(CUDAToolkit REQUIRED)
find_package(Boost REQUIRED COMPONENTS headers)
find_package(SDL2 REQUIRED COMPONENTS SDL2-static)
find_package(OpenCV CONFIG REQUIRED)

# CUDA
set(CMAKE_CUDA17_STANDARD_COMPILE_OPTION -std=c++20 CACHE STRING "" FORCE)
add_library(eye STATIC src/horus/eye.hpp src/horus/eye.cu)
target_compile_features(eye PRIVATE cxx_std_23)
target_include_directories(eye PRIVATE src)

set_target_properties(eye PROPERTIES
  CUDA_RUNTIME_LIBRARY $<IF:$<CONFIG:Debug>,Shared,Static>
  CUDA_ARCHITECTURES 50)

target_compile_options(eye PRIVATE -use_fast_math)
target_compile_definitions(eye PRIVATE BOOST_MATH_STANDALONE=1)
target_include_directories(eye PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(eye PRIVATE Boost::headers)
target_link_libraries(eye PUBLIC CUDA::cudart)

# Horus
file(GLOB_RECURSE headers src/horus/*.hpp)
file(GLOB_RECURSE sources src/horus/*.cpp)

add_library(horus SHARED ${headers} ${sources} src/demo.cpp src/main.manifest)
target_compile_features(horus PRIVATE cxx_std_23)
target_include_directories(horus PRIVATE src)

set_target_properties(horus PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

target_compile_definitions(horus PRIVATE HORUS_EXPORTS)
target_compile_definitions(horus PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(horus PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_definitions(horus PRIVATE DIRECTINPUT_VERSION=0x0800)
target_compile_options(horus PRIVATE /permissive- /FC /MP16)

target_compile_definitions(horus PRIVATE BOOST_MATH_STANDALONE=1)
target_include_directories(horus PRIVATE ${OpenCV_INCLUDE_DIRS})
target_include_directories(horus PRIVATE "C:/OBS/src/libobs")
target_link_libraries(horus PRIVATE SDL2::SDL2-static)
target_link_libraries(horus PRIVATE ${OpenCV_LIBS})
target_link_libraries(horus PRIVATE Boost::headers)
target_link_libraries(horus PRIVATE eye)

set_target_properties(horus PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_COVERAGE "C:/OBS/obs-plugins/64bit")

# Demo
add_executable(demo src/main.cpp src/main.manifest src/main.rc)
target_compile_features(demo PRIVATE cxx_std_23)
target_include_directories(demo PRIVATE src)
add_dependencies(demo horus)

# Terminate
add_executable(terminate src/terminate.cpp src/main.manifest)
target_compile_features(terminate PRIVATE cxx_std_23)

add_custom_command(TARGET horus PRE_BUILD COMMAND $<TARGET_FILE:terminate>)