#include <rock/client.hpp>
#include <iostream>

int main(int argc, char* argv[])
{
  try {
    rock::client client;
    client.mask(rock::button::left, std::chrono::milliseconds(7));
  }
  catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}