#include <exception>
#include <iostream>
#include <memory>

#include "emulator.hpp"


int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: emulator <input_file>\n";
    return 1;
  }
  try {
    ss::MemoryImage image = ss::ReadHexImage(argv[1]);
    std::unique_ptr<ss::Devices> devices = ss::CreateHostDevices();
    ss::Emulator emulator(std::move(image), *devices);
    emulator.Run();
    std::cout << emulator.StateReport();
    return 0;
  } catch (const ss::EmulatorError& error) {
    std::cerr << "emulator: " << error.what() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "emulator: unexpected failure: " << error.what() << '\n';
  }
  return 1;
}
