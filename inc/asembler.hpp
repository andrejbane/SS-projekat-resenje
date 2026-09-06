#ifndef SS_ASEMBLER_HPP_
#define SS_ASEMBLER_HPP_

#include <stdexcept>
#include <string>

#include "object.hpp"

namespace ss {


class AssemblerError : public std::runtime_error {
 public:

  explicit AssemblerError(const std::string& message)
      : std::runtime_error(message) {}
};



ObjectFile Assemble(const std::string& source, const std::string& origin);

}

#endif
