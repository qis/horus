cmake_minimum_required(VERSION 3.22 FATAL_ERROR)
project(horus DESCRIPTION "Horus" VERSION 0.1.0 LANGUAGES CXX)

# Compiler Options
string(APPEND CMAKE_CXX_FLAGS_RELEASE " /GL")

# Linker Options
foreach(LINKER SHARED MODULE EXE)
  string(APPEND CMAKE_${LINKER}_LINKER_FLAGS_RELEASE " /OPT:REF /OPT:ICF /INCREMENTAL:NO /LTCG")
endforeach()

# Conan
list(PREPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# Library
file(GLOB_RECURSE headers src/horus/*.hpp src/rock/src/client.hpp)
file(GLOB_RECURSE sources src/horus/*.cpp)

add_library(horus SHARED ${headers} ${sources} src/main.manifest)
target_include_directories(horus PRIVATE src src/rock/src)
target_compile_definitions(horus PRIVATE HORUS_EXPORTS)

target_compile_features(horus PUBLIC cxx_std_23)
target_compile_definitions(horus PUBLIC HORUS_DEBUG=$<IF:$<CONFIG:Release>,0,1>)
target_compile_definitions(horus PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_compile_definitions(horus PUBLIC WINVER=0x0A00 _WIN32_WINNT=0x0A00)
target_compile_definitions(horus PUBLIC DIRECTINPUT_VERSION=0x0800)
target_compile_options(horus PUBLIC /permissive- /FC /MP16)

target_include_directories(horus PUBLIC "C:/OBS/src/libobs")

find_package(Boost REQUIRED COMPONENTS headers)
target_link_libraries(horus PUBLIC Boost::headers)
target_compile_definitions(horus PUBLIC BOOST_MATH_STANDALONE=1)

find_package(PNG REQUIRED)
target_link_libraries(horus PUBLIC PNG::PNG)

find_package(TBB REQUIRED)
target_link_libraries(horus PUBLIC TBB::tbb)

find_package(OpenCV REQUIRED)
target_link_libraries(horus PUBLIC
  opencv::core
  opencv::imgcodecs
  opencv::imgproc)

find_package(SDL2 REQUIRED COMPONENTS SDL2-static)
target_link_libraries(horus PUBLIC SDL2::SDL2-static)

add_custom_command(TARGET horus PRE_BUILD COMMAND ${CMAKE_SOURCE_DIR}/res/kill.cmd)

# Executable
add_executable(main src/main.cpp src/main.manifest src/main.rc)
target_include_directories(main PRIVATE src src/rock/src)
set_target_properties(main PROPERTIES OUTPUT_NAME horus)
target_link_libraries(main PRIVATE horus)

set_target_properties(horus PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_COVERAGE "C:/OBS/obs-plugins/64bit")

set_target_properties(main PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "C:/OBS/"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "C:/OBS"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "C:/OBS"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "C:/OBS"
  RUNTIME_OUTPUT_DIRECTORY_COVERAGE "C:/OBS")
