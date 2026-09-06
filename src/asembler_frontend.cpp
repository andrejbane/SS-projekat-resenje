#include "asembler_frontend.hpp"

#include <limits>

#include "asembler.hpp"
#include "asembler_lexer.hpp"
#include "asembler_parser.hpp"

namespace ss {



void ValidateAssemblerSource(const std::string& source,
                             const std::string& origin) {
  if (source.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    throw AssemblerError(origin + ": source is too large for flex");
  yyscan_t scanner = nullptr;
  if (asmlex_init(&scanner) != 0)
    throw AssemblerError(origin + ": cannot initialize flex scanner");
  const std::string terminated =
      (!source.empty() && source.back() == '\n') ? source : source + '\n';
  YY_BUFFER_STATE buffer = asm_scan_bytes(
      terminated.data(), static_cast<int>(terminated.size()), scanner);
  if (buffer == nullptr) {
    asmlex_destroy(scanner);
    throw AssemblerError(origin + ": cannot initialize flex input buffer");
  }
  const int result = asmparse(scanner, origin);
  asm_delete_buffer(buffer, scanner);
  asmlex_destroy(scanner);
  if (result != 0)
    throw AssemblerError(origin + ": flex/bison source parse failed");
}

}
