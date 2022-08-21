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
file(GLOB_RECURSE headers src/horus/*.hpp)
file(GLOB_RECURSE sources src/horus/*.cpp)

add_library(horus SHARED ${headers} ${sources} src/main.manifest)
target_compile_definitions(horus PRIVATE HORUS_EXPORTS)
target_include_directories(horus PUBLIC src)

target_compile_features(horus PUBLIC cxx_std_20)
target_compile_definitions(horus PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0A00 _WIN32_WINNT=0x0A00)
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
  opencv::highgui
  opencv::imgcodecs
  opencv::imgproc)

# Executable
add_executable(main src/main.cpp src/main.manifest src/main.rc)
set_target_properties(main PROPERTIES OUTPUT_NAME horus)
target_link_libraries(main PRIVATE horus)

set_target_properties(horus main PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "C:/OBS/obs-plugins/64bit"
  RUNTIME_OUTPUT_DIRECTORY_COVERAGE "C:/OBS/obs-plugins/64bit")
