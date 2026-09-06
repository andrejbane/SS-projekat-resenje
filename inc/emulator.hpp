#ifndef SS_EMULATOR_HPP_
#define SS_EMULATOR_HPP_

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

namespace ss {

class EmulatorError : public std::runtime_error {
 public:

  explicit EmulatorError(const std::string& message)
      : std::runtime_error(message) {}
};


using MemoryImage = std::map<uint32_t, uint8_t>;


MemoryImage ParseHexImage(const std::string& text, const std::string& origin);

MemoryImage ReadHexImage(const std::string& path);


struct DeviceEvents {
  bool terminal = false;
  bool timer = false;
  uint8_t terminal_byte = 0;
};

class Devices {
 public:

  virtual ~Devices() = default;

  virtual DeviceEvents Poll() = 0;

  virtual void WriteTerminal(uint8_t value) = 0;

  virtual void ConfigureTimer(uint32_t value) = 0;
};


std::unique_ptr<Devices> CreateHostDevices();

class Emulator {
 public:

  Emulator(MemoryImage image, Devices& devices);


  bool Step();

  void Run();

  std::string StateReport() const;


  const std::array<uint32_t, 16>& registers() const { return registers_; }

  const std::array<uint32_t, 3>& csrs() const { return csrs_; }

  uint32_t InspectWord(uint32_t address) const;

 private:

  uint8_t ReadByte(uint32_t address) const;

  uint32_t ReadWord(uint32_t address) const;

  void WriteByte(uint32_t address, uint8_t value);

  void WriteWord(uint32_t address, uint32_t value);

  uint32_t ReadRegister(unsigned index) const;

  void WriteRegister(unsigned index, uint32_t value);

  void Push(uint32_t value);

  uint32_t Pop();

  void EnterInterrupt(uint32_t cause);

  void EnterSoftwareInterrupt();

  void PollAndServiceExternalInterrupts();

  void ExecuteOne();

  void InvalidInstruction();

  MemoryImage memory_;
  Devices& devices_;
  std::array<uint32_t, 16> registers_{};
  std::array<uint32_t, 3> csrs_{};
  bool halted_ = false;
  bool pending_terminal_ = false;
  bool pending_timer_ = false;
};

}

#endif
