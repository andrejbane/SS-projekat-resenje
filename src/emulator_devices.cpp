#include "emulator.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>

#ifndef _WIN32
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace ss {
namespace {

#ifndef _WIN32
termios* active_terminal_state = nullptr;


void RestoreTerminalForSignal(int signal_number) {
  if (active_terminal_state != nullptr)
    tcsetattr(STDIN_FILENO, TCSANOW, active_terminal_state);
  std::signal(signal_number, SIG_DFL);
  std::raise(signal_number);
}
#endif

class HostDevices final : public Devices {
 public:

  HostDevices() : deadline_(Clock::now() + Period(0)) {
#ifndef _WIN32
    if (isatty(STDIN_FILENO)) {
      if (tcgetattr(STDIN_FILENO, &saved_) != 0)
        throw EmulatorError(std::string("cannot read terminal settings: ") +
                            std::strerror(errno));
      termios raw = saved_;
      raw.c_lflag &= static_cast<tcflag_t>(~(ECHO | ICANON));
      raw.c_cc[VMIN] = 0;
      raw.c_cc[VTIME] = 0;
      if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0)
        throw EmulatorError(std::string("cannot configure terminal: ") +
                            std::strerror(errno));
      input_enabled_ = true;
      raw_enabled_ = true;
      active_terminal_state = &saved_;
      for (int signal_number : {SIGINT, SIGTERM, SIGQUIT, SIGHUP}) {
        const auto previous =
            std::signal(signal_number, RestoreTerminalForSignal);
        if (previous == SIG_IGN) {
          std::signal(signal_number, SIG_IGN);
        } else if (previous != SIG_ERR) {
          previous_handlers_[signal_number] = previous;
        }
      }
    }
#endif
  }


  ~HostDevices() override {
#ifndef _WIN32
    if (raw_enabled_) {
      if (tcsetattr(STDIN_FILENO, TCSANOW, &saved_) != 0)
        std::fprintf(stderr, "emulator: cannot restore terminal settings: %s\n",
                     std::strerror(errno));
      active_terminal_state = nullptr;
      for (const auto& entry : previous_handlers_)
        std::signal(entry.first, entry.second);
    }
#endif
  }


  DeviceEvents Poll() override {
    DeviceEvents events;
    const auto now = Clock::now();
    if (now >= deadline_) {
      events.timer = true;

      do {
        deadline_ += Period(timer_config_);
      } while (deadline_ <= now);
    }
#ifndef _WIN32
    if (input_enabled_) {

      for (unsigned drained = 0; drained < 256; ++drained) {
        fd_set input;
        FD_ZERO(&input);
        FD_SET(STDIN_FILENO, &input);
        timeval timeout{};
        const int ready =
            select(STDIN_FILENO + 1, &input, nullptr, nullptr, &timeout);
        if (ready == 0) break;
        if (ready < 0) {
          if (errno == EINTR) continue;
          throw EmulatorError(std::string("cannot poll terminal input: ") +
                              std::strerror(errno));
        }
        unsigned char value = 0;
        const ssize_t count = read(STDIN_FILENO, &value, 1);
        if (count == 1) {
          events.terminal = true;
          events.terminal_byte = value;
        } else if (count == 0) {
          break;
        } else if (errno != EINTR) {
          throw EmulatorError(std::string("cannot read terminal input: ") +
                              std::strerror(errno));
        }
      }
    }
#endif
    return events;
  }


  void WriteTerminal(uint8_t value) override {
    std::cout.put(static_cast<char>(value));
    std::cout.flush();
  }


  void ConfigureTimer(uint32_t value) override {
    timer_config_ = value & 7u;
    deadline_ = Clock::now() + Period(timer_config_);
  }

 private:
  using Clock = std::chrono::steady_clock;


  static std::chrono::milliseconds Period(uint32_t value) {
    static const uint32_t periods[] = {500, 1000, 1500, 2000,
                                       5000, 10000, 30000, 60000};
    return std::chrono::milliseconds(periods[value & 7u]);
  }

  uint32_t timer_config_ = 0;
  Clock::time_point deadline_;
#ifndef _WIN32
  termios saved_{};
  bool input_enabled_ = false;
  bool raw_enabled_ = false;
  std::map<int, void (*)(int)> previous_handlers_;
#endif
};

}


std::unique_ptr<Devices> CreateHostDevices() {
  return std::unique_ptr<Devices>(new HostDevices());
}

}
