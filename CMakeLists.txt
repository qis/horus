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

# Kill
add_executable(kill src/kill.cpp src/main.manifest)
target_compile_features(kill PRIVATE cxx_std_23)

# Plugin
file(GLOB_RECURSE headers src/horus/*.hpp)
file(GLOB_RECURSE sources src/horus/*.cpp)

add_library(plugin SHARED ${headers} ${sources} src/main.manifest)
target_compile_features(plugin PRIVATE cxx_std_23)
target_include_directories(plugin PRIVATE src)

set_target_properties(plugin PROPERTIES
  INTERPROCEDURAL_OPTIMIZATION_RELEASE ON
  OUTPUT_NAME horus)

target_compile_definitions(plugin PRIVATE HORUS_EXPORTS)
target_compile_definitions(plugin PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(plugin PRIVATE WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_definitions(plugin PRIVATE DIRECTINPUT_VERSION=0x0800)
target_compile_options(plugin PRIVATE /permissive- /FC /MP16)

target_compile_definitions(plugin PRIVATE BOOST_MATH_STANDALONE=1)
target_include_directories(plugin PRIVATE ${OpenCV_INCLUDE_DIRS})
target_include_directories(plugin PRIVATE "C:/OBS/src/libobs")
target_link_libraries(plugin PRIVATE SDL2::SDL2-static)
target_link_libraries(plugin PRIVATE ${OpenCV_LIBS})
target_link_libraries(plugin PRIVATE Boost::headers)
target_link_libraries(plugin PRIVATE eye)

add_custom_command(TARGET plugin PRE_BUILD COMMAND $<TARGET_FILE:kill>)

set_target_properties(plugin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_COVERAGE "C:/OBS/obs-plugins/64bit")

# Executable
add_executable(horus src/main.cpp src/main.manifest src/main.rc)
target_compile_features(horus PRIVATE cxx_std_23)
target_include_directories(horus PRIVATE src)
add_dependencies(horus plugin)

set_target_properties(horus PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "C:/OBS/"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "C:/OBS"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "C:/OBS"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "C:/OBS"
  RUNTIME_OUTPUT_DIRECTORY_COVERAGE "C:/OBS")
