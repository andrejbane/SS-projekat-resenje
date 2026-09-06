 #include "emulator.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <utility>

namespace ss {
namespace {

constexpr uint32_t kInitialPc = 0x40000000u;
constexpr uint32_t kTermOut = 0xFFFFFF00u;
constexpr uint32_t kTermIn = 0xFFFFFF04u;
constexpr uint32_t kTimerConfig = 0xFFFFFF10u;


int HexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}


uint32_t ParseAddress(const std::string& text, const std::string& origin,
                      size_t line) {
  if (text.size() != 8)
    throw EmulatorError(origin + ":" + std::to_string(line) +
                        ": address must contain exactly eight hex digits");
  uint32_t value = 0;
  for (char c : text) {
    const int digit = HexDigit(c);
    if (digit < 0)
      throw EmulatorError(origin + ":" + std::to_string(line) +
                          ": invalid hex address '" + text + "'");
    value = (value << 4) | static_cast<uint32_t>(digit);
  }
  return value;
}


uint8_t ParseByte(const std::string& text, const std::string& origin,
                  size_t line) {
  if (text.size() != 2 || HexDigit(text[0]) < 0 || HexDigit(text[1]) < 0)
    throw EmulatorError(origin + ":" + std::to_string(line) +
                        ": expected a two-digit hex byte, got '" + text + "'");
  return static_cast<uint8_t>((HexDigit(text[0]) << 4) | HexDigit(text[1]));
}

}


MemoryImage ParseHexImage(const std::string& text, const std::string& origin) {
  MemoryImage image;
  std::istringstream input(text);
  std::string row;
  size_t line = 0;
  while (std::getline(input, row)) {
    ++line;
    if (!row.empty() && row.back() == '\r') row.pop_back();
    if (row.empty())
      throw EmulatorError(origin + ":" + std::to_string(line) +
                          ": blank lines are not valid hex records");
    const size_t colon = row.find(':');
    if (colon == std::string::npos || row.find(':', colon + 1) != std::string::npos)
      throw EmulatorError(origin + ":" + std::to_string(line) +
                          ": expected 'AAAAAAAA: bb ...'");
    const uint32_t base = ParseAddress(row.substr(0, colon), origin, line);
    if (colon + 2 > row.size() || row[colon + 1] != ' ')
      throw EmulatorError(origin + ":" + std::to_string(line) +
                          ": expected one space after ':'");
    std::istringstream bytes(row.substr(colon + 2));
    std::string token;
    size_t count = 0;
    while (bytes >> token) {
      if (++count > 8)
        throw EmulatorError(origin + ":" + std::to_string(line) +
                            ": a hex record may contain at most eight bytes");
      const uint64_t address = static_cast<uint64_t>(base) + count - 1;
      if (address > 0xFFFFFFFFull)
        throw EmulatorError(origin + ":" + std::to_string(line) +
                            ": hex record runs past the 32-bit address space");
      const uint32_t key = static_cast<uint32_t>(address);
      if (image.count(key))
        throw EmulatorError(origin + ":" + std::to_string(line) +
                            ": overlapping byte at address 0x" +

                            [&] {
                              std::ostringstream out;
                              out << std::hex << std::uppercase << std::setw(8)
                                  << std::setfill('0') << key;
                              return out.str();
                            }());
      image[key] = ParseByte(token, origin, line);
    }
    if (count == 0)
      throw EmulatorError(origin + ":" + std::to_string(line) +
                          ": a hex record must contain at least one byte");
  }
  if (line == 0) throw EmulatorError(origin + ": empty hex image");
  return image;
}


MemoryImage ReadHexImage(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw EmulatorError("cannot open input file '" + path + "'");
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  if (stream.bad()) throw EmulatorError("error while reading '" + path + "'");
  return ParseHexImage(buffer.str(), path);
}


Emulator::Emulator(MemoryImage image, Devices& devices)
    : memory_(std::move(image)), devices_(devices) {
  registers_[15] = kInitialPc;
  WriteWord(kTimerConfig, 0);
}


uint8_t Emulator::ReadByte(uint32_t address) const {
  const auto found = memory_.find(address);
  return found == memory_.end() ? 0 : found->second;
}


uint32_t Emulator::ReadWord(uint32_t address) const {
  uint32_t value = 0;
  for (unsigned byte = 0; byte < 4; ++byte)
    value |= static_cast<uint32_t>(ReadByte(address + byte)) << (8 * byte);
  return value;
}


uint32_t Emulator::InspectWord(uint32_t address) const {
  return ReadWord(address);
}


void Emulator::WriteByte(uint32_t address, uint8_t value) {
  memory_[address] = value;
}


void Emulator::WriteWord(uint32_t address, uint32_t value) {
  for (unsigned byte = 0; byte < 4; ++byte)
    WriteByte(address + byte, static_cast<uint8_t>(value >> (8 * byte)));
  if (address == kTermOut) devices_.WriteTerminal(static_cast<uint8_t>(value));
  if (address == kTimerConfig) devices_.ConfigureTimer(value);
}


uint32_t Emulator::ReadRegister(unsigned index) const {
  return index == 0 ? 0 : registers_.at(index);
}


void Emulator::WriteRegister(unsigned index, uint32_t value) {
  if (index != 0) registers_.at(index) = value;
}


void Emulator::Push(uint32_t value) {
  registers_[14] -= 4;
  WriteWord(registers_[14], value);
}


uint32_t Emulator::Pop() {
  const uint32_t value = ReadWord(registers_[14]);
  registers_[14] += 4;
  return value;
}


void Emulator::EnterInterrupt(uint32_t cause) {
  const uint32_t old_status = csrs_[0];
  Push(old_status);
  Push(registers_[15]);
  csrs_[2] = cause;
  csrs_[0] = old_status | 4u;
  registers_[15] = csrs_[1];
}


void Emulator::EnterSoftwareInterrupt() {
  const uint32_t old_status = csrs_[0];
  Push(old_status);
  Push(registers_[15]);
  csrs_[2] = 4;
  csrs_[0] = old_status & ~1u;
  registers_[15] = csrs_[1];
}


void Emulator::InvalidInstruction() { EnterInterrupt(1); }


void Emulator::PollAndServiceExternalInterrupts() {
  const DeviceEvents events = devices_.Poll();
  if (events.terminal) {
    for (unsigned byte = 0; byte < 4; ++byte)
      memory_[kTermIn + byte] =
          static_cast<uint8_t>(events.terminal_byte >> (8 * byte));
    pending_terminal_ = true;
  }
  if (events.timer) pending_timer_ = true;
  if (csrs_[0] & 4u) return;
  if (pending_terminal_ && !(csrs_[0] & 2u)) {
    pending_terminal_ = false;
    EnterInterrupt(3);
  } else if (pending_timer_ && !(csrs_[0] & 1u)) {
    pending_timer_ = false;
    EnterInterrupt(2);
  }
}


void Emulator::ExecuteOne() {
  const uint32_t pc = registers_[15];
  const uint8_t opmod = ReadByte(pc);
  const uint8_t ab = ReadByte(pc + 1);
  const uint8_t cd = ReadByte(pc + 2);
  const uint8_t d0 = ReadByte(pc + 3);

  registers_[15] += 4;

  const unsigned op = opmod >> 4;
  const unsigned mod = opmod & 15;
  const unsigned a = ab >> 4;
  const unsigned b = ab & 15;
  const unsigned c = cd >> 4;
  int32_t displacement = static_cast<int32_t>(((cd & 15u) << 8) | d0);
  if (displacement & 0x800) displacement |= ~0xFFF;
  const uint32_t d = static_cast<uint32_t>(displacement);

  if (op == 0 && mod == 0 && a == 0 && b == 0 && c == 0 && d0 == 0 &&
      (cd & 15u) == 0) {
    halted_ = true;
    return;
  }
  if (op == 1 && mod == 0 && a == 0 && b == 0 && c == 0 && d0 == 0 &&
      (cd & 15u) == 0) {
    EnterSoftwareInterrupt();
    return;
  }
  if (op == 2 && (mod == 0 || mod == 1) && c == 0) {
    Push(registers_[15]);
    const uint32_t address = ReadRegister(a) + ReadRegister(b) + d;
    registers_[15] = mod == 0 ? address : ReadWord(address);
    return;
  }
  if (op == 3) {
    bool condition = true;
    if (((mod & 7u) == 0) && (b != 0 || c != 0)) {
      InvalidInstruction();
      return;
    }
    if ((mod & 7u) >= 1 && (mod & 7u) <= 3) {
      if ((mod & 7u) == 1) condition = ReadRegister(b) == ReadRegister(c);
      if ((mod & 7u) == 2) condition = ReadRegister(b) != ReadRegister(c);
      if ((mod & 7u) == 3)
        condition = static_cast<int32_t>(ReadRegister(b)) >
                    static_cast<int32_t>(ReadRegister(c));
    } else if ((mod & 7u) != 0) {
      InvalidInstruction();
      return;
    }
    if (condition) {
      const uint32_t address = ReadRegister(a) + d;
      registers_[15] = (mod & 8u) ? ReadWord(address) : address;
    }
    return;
  }
  if (op == 4 && mod == 0 && a == 0 && displacement == 0) {
    const uint32_t temporary = ReadRegister(b);
    WriteRegister(b, ReadRegister(c));
    WriteRegister(c, temporary);
    return;
  }
  if (op == 5 && mod <= 3 && displacement == 0) {
    const uint32_t lhs = ReadRegister(b);
    const uint32_t rhs = ReadRegister(c);
    if (mod == 0) WriteRegister(a, lhs + rhs);
    if (mod == 1) WriteRegister(a, lhs - rhs);
    if (mod == 2) WriteRegister(a, lhs * rhs);
    if (mod == 3) {
      const int32_t signed_lhs = static_cast<int32_t>(lhs);
      const int32_t signed_rhs = static_cast<int32_t>(rhs);
      if (signed_rhs == 0 ||
          (signed_lhs == std::numeric_limits<int32_t>::min() && signed_rhs == -1)) {
        InvalidInstruction();
        return;
      }
      WriteRegister(a, static_cast<uint32_t>(signed_lhs / signed_rhs));
    }
    return;
  }
  if (op == 6 && mod <= 3 && displacement == 0 &&
      (mod != 0 || c == 0)) {
    if (mod == 0) WriteRegister(a, ~ReadRegister(b));
    if (mod == 1) WriteRegister(a, ReadRegister(b) & ReadRegister(c));
    if (mod == 2) WriteRegister(a, ReadRegister(b) | ReadRegister(c));
    if (mod == 3) WriteRegister(a, ReadRegister(b) ^ ReadRegister(c));
    return;
  }
  if (op == 7 && mod <= 1 && displacement == 0) {
    const unsigned count = ReadRegister(c) & 31u;
    WriteRegister(a, mod == 0 ? ReadRegister(b) << count
                              : ReadRegister(b) >> count);
    return;
  }
  if (op == 8 && (mod == 0 || mod == 1 || mod == 2)) {
    if (mod == 1) {
      if (b != 0) {
        InvalidInstruction();
        return;
      }
      WriteRegister(a, ReadRegister(a) + d);
      WriteWord(ReadRegister(a), ReadRegister(c));
    } else {
      const uint32_t address = ReadRegister(a) + ReadRegister(b) + d;
      WriteWord(mod == 2 ? ReadWord(address) : address, ReadRegister(c));
    }
    return;
  }
  if (op == 9) {
    if (mod == 0 && b < 3 && c == 0 && displacement == 0)
      WriteRegister(a, csrs_[b]);
    else if (mod == 1 && c == 0)
      WriteRegister(a, ReadRegister(b) + d);
    else if (mod == 2) WriteRegister(a, ReadWord(ReadRegister(b) + ReadRegister(c) + d));
    else if (mod == 3 && c == 0) {
      WriteRegister(a, ReadWord(ReadRegister(b)));
      WriteRegister(b, ReadRegister(b) + d);
    } else if (mod == 4 && a < 3 && c == 0 && displacement == 0)
      csrs_[a] = ReadRegister(b);
    else if (mod == 5 && a < 3 && b < 3 && c == 0)
      csrs_[a] = csrs_[b] | d;
    else if (mod == 6 && a < 3) csrs_[a] = ReadWord(ReadRegister(b) + ReadRegister(c) + d);
    else if (mod == 7 && a < 3 && c == 0) {
      csrs_[a] = ReadWord(ReadRegister(b));
      WriteRegister(b, ReadRegister(b) + d);
    } else {
      InvalidInstruction();
    }
    return;
  }
  InvalidInstruction();
}


bool Emulator::Step() {
  if (halted_) return false;
  ExecuteOne();
  registers_[0] = 0;
  if (!halted_) PollAndServiceExternalInterrupts();
  return !halted_;
}


void Emulator::Run() {
  while (Step()) {
  }
}


std::string Emulator::StateReport() const {
  std::ostringstream out;
  out << "-----------------------------------------------------------------\n"
      << "Emulated processor executed halt instruction\n"
      << "Emulated processor state:\n";
  out << std::hex << std::nouppercase << std::setfill('0');
  for (unsigned row = 0; row < 4; ++row) {
    for (unsigned column = 0; column < 4; ++column) {
      const unsigned index = row * 4 + column;
      out << (index < 10 ? " r" : "r") << std::dec << index << "=0x"
          << std::hex << std::setw(8) << ReadRegister(index);
      if (column != 3) out << "   ";
    }
    out << '\n';
  }
  return out.str();
}

}
