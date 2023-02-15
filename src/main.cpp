#include <windows.h>
#include <stdexcept>
#include <cstdio>

int main(int argc, char* argv[])
{
  try {
    SetDllDirectory("C:/OBS/obs-plugins/64bit");
    const auto library = LoadLibrary("horus.dll");
    if (!library) {
      throw std::runtime_error("could not load library");
    }
    const auto demo = GetProcAddress(library, "demo");
    if (!demo) {
      throw std::runtime_error("could not get demo address");
    }
    return reinterpret_cast<int (*)(int argc, char* argv[])>(demo)(argc, argv);
  }
  catch (const std::exception& e) {
    std::fputs(e.what(), stderr);
    std::fputs("\r\n", stderr);
    std::fflush(stderr);
  }
  return EXIT_FAILURE;
}