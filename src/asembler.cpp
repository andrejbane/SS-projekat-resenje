#include "asembler.hpp"

#ifdef SS_USE_FLEX_BISON
#include "asembler_frontend.hpp"
#endif

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

namespace ss {
namespace {



struct Linear {
  int64_t constant = 0;
  std::map<std::string, int64_t> terms;
  bool external_used = false;
};



struct SymbolState {
  bool defined = false;
  bool global = false;
  bool external = false;
  bool absolute = false;
  bool evaluating = false;
  bool evaluated = false;
  uint32_t section = kSectionUndefined;
  uint32_t value = 0;
  std::string expression;
  size_t line = 0;
};



struct WordFixup {
  uint32_t section = 0;
  uint32_t offset = 0;
  std::string expression;
  size_t line = 0;
};



struct PoolReference {
  uint32_t instruction_offset = 0;
  size_t pool_index = 0;
  size_t line = 0;
};



struct PoolEntry {
  std::string expression;
  uint32_t offset = 0;
  size_t line = 0;
};



struct SectionState {
  Section section;
  std::vector<PoolEntry> pool;
  std::map<std::string, size_t> pool_by_expression;
  std::vector<PoolReference> pool_references;
};



struct DisplacementFixup {
  uint32_t section = 0;
  uint32_t instruction_offset = 0;
  std::string expression;
  size_t line = 0;
};



struct PendingLabel {
  std::string name;
  uint32_t section = 0;
  size_t line = 0;
};



struct MemoryAddress {
  int base = 0;
  int index = 0;
  std::string displacement = "0";
};


std::string Trim(const std::string& value) {
  const size_t first = value.find_first_not_of(" \t\r");
  if (first == std::string::npos) return "";
  const size_t last = value.find_last_not_of(" \t\r");
  return value.substr(first, last - first + 1);
}


bool IsName(const std::string& value) {
  if (value.empty() ||
      !(std::isalpha(static_cast<unsigned char>(value[0])) ||
        value[0] == '_' || value[0] == '.'))
    return false;
  for (char c : value)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'))
      return false;
  return true;
}



std::vector<std::string> SplitOperands(const std::string& text) {
  std::vector<std::string> result;
  size_t start = 0;
  int brackets = 0;
  bool quoted = false;
  bool escaped = false;
  for (size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else if (c == '"') {
      quoted = true;
    } else if (c == '[' || c == '(') {
      ++brackets;
    } else if (c == ']' || c == ')') {
      --brackets;
    } else if (c == ',' && brackets == 0) {
      result.push_back(Trim(text.substr(start, i - start)));
      start = i + 1;
    }
  }
  result.push_back(Trim(text.substr(start)));
  return result;
}

class ExpressionParser {
 public:


  explicit ExpressionParser(const std::string& text) : text_(text) {}



  Linear Parse() {
    Linear value = ParseSum();
    Skip();
    if (position_ != text_.size())
      throw std::runtime_error("unexpected token near '" + text_.substr(position_) + "'");
    return value;
  }

 private:

  void Skip() {
    while (position_ < text_.size() &&
           std::isspace(static_cast<unsigned char>(text_[position_])))
      ++position_;
  }



  Linear ParseSum() {
    Linear result = ParseUnary();
    while (true) {
      Skip();
      if (position_ == text_.size() ||
          (text_[position_] != '+' && text_[position_] != '-'))
        return result;
      const int sign = text_[position_++] == '+' ? 1 : -1;
      Linear rhs = ParseUnary();
      result.constant += sign * rhs.constant;
      result.external_used = result.external_used || rhs.external_used;
      for (const auto& term : rhs.terms)
        result.terms[term.first] += sign * term.second;
    }
  }


  Linear ParseUnary() {
    Skip();
    int sign = 1;
    while (position_ < text_.size() &&
           (text_[position_] == '+' || text_[position_] == '-')) {
      if (text_[position_++] == '-') sign = -sign;
      Skip();
    }
    Linear value = ParsePrimary();
    value.constant *= sign;
    for (auto& term : value.terms) term.second *= sign;
    return value;
  }



  Linear ParsePrimary() {
    Skip();
    if (position_ >= text_.size()) throw std::runtime_error("expected expression term");
    if (text_[position_] == '(') {
      ++position_;
      Linear value = ParseSum();
      Skip();
      if (position_ >= text_.size() || text_[position_] != ')')
        throw std::runtime_error("missing ')' in expression");
      ++position_;
      return value;
    }
    const size_t start = position_;
    if (std::isdigit(static_cast<unsigned char>(text_[position_]))) {
      while (position_ < text_.size() &&
             (std::isalnum(static_cast<unsigned char>(text_[position_])) ||
              text_[position_] == 'x' || text_[position_] == 'X'))
        ++position_;
      const std::string token = text_.substr(start, position_ - start);
      unsigned long long value = 0;
      try {
        const bool hexadecimal =
            token.size() > 2 && token[0] == '0' &&
            (token[1] == 'x' || token[1] == 'X');
        const size_t digits = hexadecimal ? 2 : 0;
        if (digits == token.size())
          throw std::invalid_argument("missing hexadecimal digits");
        for (size_t i = digits; i < token.size(); ++i) {
          const unsigned char c = static_cast<unsigned char>(token[i]);
          if (hexadecimal ? !std::isxdigit(c) : !std::isdigit(c))
            throw std::invalid_argument("invalid digit");
        }
        value = std::stoull(token.substr(digits), nullptr,
                            hexadecimal ? 16 : 10);
      } catch (const std::exception&) {
        throw std::runtime_error("malformed literal '" + token + "'");
      }
      if (value > 0xFFFFFFFFull)
        throw std::runtime_error("literal out of 32-bit range: '" + token + "'");
      Linear result;
      result.constant = static_cast<int64_t>(value);
      return result;
    }
    while (position_ < text_.size() &&
           (std::isalnum(static_cast<unsigned char>(text_[position_])) ||
            text_[position_] == '_' || text_[position_] == '.'))
      ++position_;
    const std::string name = text_.substr(start, position_ - start);
    if (!IsName(name)) throw std::runtime_error("invalid symbol in expression");
    Linear result;
    result.terms[name] = 1;
    return result;
  }

  std::string text_;
  size_t position_ = 0;
};

class Assembler {
 public:


  Assembler(std::string source, std::string origin)
      : source_(std::move(source)), origin_(std::move(origin)) {}



  ObjectFile Run() {
    ParseLines();
    ResolveEquations();
    FlushAllPools();
    ResolveDisplacements();
    BuildObject();
    return std::move(object_);
  }

 private:

  [[noreturn]] void Error(size_t line, const std::string& message) const {
    throw AssemblerError(origin_ + ":" + std::to_string(line) + ": " + message);
  }



  uint32_t RequireSection(size_t line) const {
    if (current_section_ == kSectionUndefined)
      Error(line, "statement requires an active .section");
    return current_section_;
  }



  SymbolState& GetSymbol(const std::string& name) {
    if (!IsName(name)) throw std::runtime_error("invalid symbol name '" + name + "'");
    if (!symbols_.count(name)) symbol_order_.push_back(name);
    return symbols_[name];
  }


  static bool Fits12(int64_t value) { return value >= -2048 && value <= 2047; }



  void EmitInstruction(uint8_t opmod, int a, int b, int c, int32_t displacement,
                       size_t line, bool manage_pool = true) {
    const uint32_t section = RequireSection(line);
    if (manage_pool) {
      EnsurePoolReach(section, 4, line);
      BindPendingLabels(section);
    }
    if (!Fits12(displacement)) Error(line, "instruction displacement does not fit signed 12 bits");
    const uint16_t d = static_cast<uint16_t>(displacement) & 0x0FFFu;
    std::vector<uint8_t>& data = sections_[section].section.data;
    data.push_back(opmod);
    data.push_back(static_cast<uint8_t>((a << 4) | b));
    data.push_back(static_cast<uint8_t>((c << 4) | ((d >> 8) & 0x0F)));
    data.push_back(static_cast<uint8_t>(d & 0xFF));
  }



  void EmitPoolInstruction(uint8_t opmod, int a, int b, int c,
                           const std::string& expression, size_t line) {
    const uint32_t section = RequireSection(line);
    EnsurePoolReach(section, 8, line);
    BindPendingLabels(section);
    const uint32_t instruction = static_cast<uint32_t>(
        sections_[section].section.data.size());
    EmitInstruction(opmod, a, b, c, 0, line, false);
    SectionState& state = sections_[section];
    auto found = state.pool_by_expression.find(expression);
    size_t index;
    if (found == state.pool_by_expression.end()) {
      index = state.pool.size();
      state.pool_by_expression[expression] = index;
      state.pool.push_back(PoolEntry{expression, 0, line});
    } else {
      index = found->second;
    }
    state.pool_references.push_back(
        PoolReference{instruction, index, line});
  }



  void PatchDisplacement(SectionState& state, uint32_t instruction,
                         int64_t displacement, size_t line) {
    if (!Fits12(displacement))
      Error(line, "generated displacement does not fit signed 12 bits");
    const uint16_t d = static_cast<uint16_t>(displacement) & 0x0FFFu;
    state.section.data[instruction + 3] = static_cast<uint8_t>(d & 0xFF);
    state.section.data[instruction + 2] =
        static_cast<uint8_t>((state.section.data[instruction + 2] & 0xF0) |
                             ((d >> 8) & 0x0F));
  }



  void EnsurePoolReach(uint32_t section, uint32_t upcoming, size_t line) {
    SectionState& state = sections_[section];
    if (state.pool_references.empty()) return;
    const uint64_t pool_end = static_cast<uint64_t>(state.section.data.size()) +
                              upcoming + 4 + state.pool.size() * 4;
    const uint64_t first_pc =
        static_cast<uint64_t>(state.pool_references.front().instruction_offset) + 4;
    if (pool_end - 4 - first_pc > 2047) FlushPool(section, line);
  }



  void FlushPool(uint32_t section, size_t line) {
    SectionState& state = sections_[section];
    if (state.pool.empty()) return;


    const uint32_t jump = static_cast<uint32_t>(state.section.data.size());
    current_section_ = section;
    EmitInstruction(0x30, 15, 0, 0,
                    static_cast<int32_t>(state.pool.size() * 4), line, false);
    for (PoolEntry& entry : state.pool) {
      entry.offset = static_cast<uint32_t>(state.section.data.size());
      state.section.data.insert(state.section.data.end(), 4, 0);
      word_fixups_.push_back(
          WordFixup{section, entry.offset, entry.expression, entry.line});
    }
    for (const PoolReference& reference : state.pool_references) {
      const PoolEntry& entry = state.pool.at(reference.pool_index);
      PatchDisplacement(state, reference.instruction_offset,
                        static_cast<int64_t>(entry.offset) -
                            (reference.instruction_offset + 4),
                        reference.line);
    }
    (void)jump;
    state.pool.clear();
    state.pool_by_expression.clear();
    state.pool_references.clear();
  }


  int Register(const std::string& text, size_t line) const {
    std::string value = Trim(text);
    if (value.empty() || value[0] != '%') Error(line, "expected general register");
    value.erase(value.begin());
    if (value == "sp") return 14;
    if (value == "pc") return 15;
    if (value.size() >= 2 && value[0] == 'r') {
      for (size_t i = 1; i < value.size(); ++i)
        if (!std::isdigit(static_cast<unsigned char>(value[i])))
          Error(line, "invalid register '%" + value + "'");
      size_t consumed = 0;
      int number = -1;
      try {
        number = std::stoi(value.substr(1), &consumed);
      } catch (const std::exception&) {
        Error(line, "invalid register '%" + value + "'");
      }
      if (consumed == value.size() - 1 && number >= 0 && number <= 15)
        return number;
    }
    Error(line, "invalid register '%" + value + "'");
  }


  int Csr(const std::string& text, size_t line) const {
    const std::string value = Trim(text);
    if (value == "%status") return 0;
    if (value == "%handler") return 1;
    if (value == "%cause") return 2;
    Error(line, "invalid CSR '" + value + "'");
  }



  bool Literal(const std::string& text, int64_t* value) const {
    try {
      Linear expression = ExpressionParser(text).Parse();
      if (!expression.terms.empty()) return false;
      *value = expression.constant;
      return true;
    } catch (const std::exception&) {
      return false;
    }
  }



  void DefineLabel(const std::string& name, uint32_t section, size_t line) {
    current_section_ = section;
    SymbolState& symbol = GetSymbol(name);
    if (symbol.line == 0) symbol.line = line;
    if (symbol.external) Error(line, "external symbol '" + name + "' cannot be defined locally");
    if (symbol.defined) Error(line, "multiple definition of symbol '" + name + "'");
    symbol.defined = true;
    symbol.section = section;
    symbol.value = static_cast<uint32_t>(sections_[section].section.data.size());
    symbol.line = line;
  }



  void QueueLabel(const std::string& name, size_t line) {
    const uint32_t section = RequireSection(line);
    pending_labels_.push_back(PendingLabel{name, section, line});
  }



  void BindPendingLabels(uint32_t section) {
    for (const PendingLabel& label : pending_labels_) {
      if (label.section != section)
        Error(label.line, "label cannot cross a section boundary");
      DefineLabel(label.name, label.section, label.line);
    }
    pending_labels_.clear();
  }




  void ParseLines() {
    std::istringstream input(source_);
    std::string raw;
    size_t line = 0;
    while (!ended_ && std::getline(input, raw)) {
      ++line;
      if (!raw.empty() && raw.back() == '\r') raw.pop_back();
      bool quoted = false;
      bool escaped = false;
      size_t comment = std::string::npos;
      for (size_t i = 0; i < raw.size(); ++i) {
        if (quoted) {
          if (escaped) escaped = false;
          else if (raw[i] == '\\') escaped = true;
          else if (raw[i] == '"') quoted = false;
        } else if (raw[i] == '"') {
          quoted = true;
        } else if (raw[i] == '#') {
          comment = i;
          break;
        }
      }
      std::string text = Trim(raw.substr(0, comment));
      if (text.empty()) continue;

      const size_t colon = text.find(':');
      const size_t first_blank = text.find_first_of(" \t");
      if (colon != std::string::npos &&
          (first_blank == std::string::npos || colon < first_blank)) {
        const std::string label = Trim(text.substr(0, colon));
        if (!IsName(label)) Error(line, "invalid label '" + label + "'");
        QueueLabel(label, line);
        text = Trim(text.substr(colon + 1));
        if (text.empty()) continue;
      }

      const size_t blank = text.find_first_of(" \t");
      const std::string operation = text.substr(0, blank);
      const std::string rest =
          blank == std::string::npos ? "" : Trim(text.substr(blank + 1));
      if (!operation.empty() && operation[0] == '.')
        Directive(operation, rest, line);
      else
        Instruction(operation, rest, line);
    }
    if (!pending_labels_.empty()) {
      const uint32_t section = RequireSection(line);
      BindPendingLabels(section);
      FlushPool(section, line);
    }
  }




  void Directive(const std::string& operation, const std::string& rest,
                 size_t line) {
    if (operation == ".section") {
      if (!IsName(rest)) Error(line, "invalid section name '" + rest + "'");
      if (current_section_ != kSectionUndefined) {
        if (!pending_labels_.empty())
          BindPendingLabels(current_section_);
        FlushPool(current_section_, line);
      }
      auto found = section_by_name_.find(rest);
      if (found == section_by_name_.end()) {
        current_section_ = static_cast<uint32_t>(sections_.size());
        section_by_name_[rest] = current_section_;
        SectionState state;
        state.section.name = rest;
        sections_.push_back(std::move(state));
      } else {
        current_section_ = found->second;
      }
      return;
    }
    if (operation == ".global" || operation == ".extern") {
      for (const std::string& name : SplitOperands(rest)) {
        if (!IsName(name)) Error(line, "invalid symbol name '" + name + "'");
        SymbolState& symbol = GetSymbol(name);
        if (operation == ".global") {
          if (symbol.external) Error(line, "symbol '" + name + "' cannot be both .global and .extern");
          symbol.global = true;
        } else {
          if (symbol.global || symbol.defined)
            Error(line, "symbol '" + name + "' cannot be both local/global and .extern");
          symbol.external = true;
        }
      }
      return;
    }
    if (operation == ".word") {
      const uint32_t section = RequireSection(line);
      const std::vector<std::string> values = SplitOperands(rest);
      if (values.empty() || (values.size() == 1 && values[0].empty()))
        Error(line, ".word requires at least one initializer");
      if (values.size() > 0xFFFFFFFFull / 4)
        Error(line, ".word initializer list is too large");
      EnsurePoolReach(section, static_cast<uint32_t>(values.size() * 4), line);
      BindPendingLabels(section);
      for (const std::string& expression : values) {
        const uint32_t offset =
            static_cast<uint32_t>(sections_[section].section.data.size());
        sections_[section].section.data.insert(
            sections_[section].section.data.end(), 4, 0);
        word_fixups_.push_back(WordFixup{section, offset, expression, line});
      }
      return;
    }
    if (operation == ".skip") {
      const uint32_t section = RequireSection(line);
      int64_t count = 0;
      if (!Literal(rest, &count) || count < 0 ||
          static_cast<uint64_t>(count) > 0xFFFFFFFFull -
              sections_[section].section.data.size())
        Error(line, ".skip requires a non-negative 32-bit literal");
      EnsurePoolReach(section, static_cast<uint32_t>(count), line);
      BindPendingLabels(section);
      sections_[section].section.data.insert(
          sections_[section].section.data.end(), static_cast<size_t>(count), 0);
      return;
    }
    if (operation == ".ascii") {
      const uint32_t section = RequireSection(line);
      if (rest.size() < 2 || rest.front() != '"' || rest.back() != '"')
        Error(line, ".ascii requires one quoted string");
      std::vector<uint8_t> decoded;
      for (size_t i = 1; i + 1 < rest.size(); ++i) {
        char c = rest[i];
        if (c == '\\') {
          if (++i + 1 >= rest.size()) Error(line, "unterminated escape in .ascii");
          switch (rest[i]) {
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case '\\': c = '\\'; break;
            case '"': c = '"'; break;
            case '0': c = '\0'; break;
            default: Error(line, "unsupported escape in .ascii");
          }
        }
        decoded.push_back(static_cast<uint8_t>(c));
      }
      if (decoded.size() > 0xFFFFFFFFull)
        Error(line, ".ascii value is too large");
      EnsurePoolReach(section, static_cast<uint32_t>(decoded.size()), line);
      BindPendingLabels(section);
      sections_[section].section.data.insert(
          sections_[section].section.data.end(), decoded.begin(), decoded.end());
      return;
    }
    if (operation == ".equ") {
      const std::vector<std::string> fields = SplitOperands(rest);
      if (fields.size() != 2 || !IsName(fields[0]) || fields[1].empty())
        Error(line, "expected '.equ <symbol>, <expression>'");
      SymbolState& symbol = GetSymbol(fields[0]);
      if (symbol.external) Error(line, "external symbol '" + fields[0] + "' cannot be defined by .equ");
      if (symbol.defined) Error(line, "multiple definition of symbol '" + fields[0] + "'");
      symbol.defined = true;
      symbol.expression = fields[1];
      symbol.line = line;
      return;
    }
    if (operation == ".end") {
      if (!rest.empty()) Error(line, ".end takes no operands");
      if (current_section_ != kSectionUndefined) {
        if (!pending_labels_.empty())
          BindPendingLabels(current_section_);
        FlushPool(current_section_, line);
      }
      ended_ = true;
      return;
    }
    Error(line, "unknown directive '" + operation + "'");
  }




  void Instruction(const std::string& op, const std::string& rest, size_t line) {
    const std::vector<std::string> args =
        rest.empty() ? std::vector<std::string>() : SplitOperands(rest);

    auto require = [&](size_t count) {
      if (args.size() != count)
        Error(line, "instruction '" + op + "' expects " +
                        std::to_string(count) + " operand(s)");
    };
    if (op == "halt") { require(0); EmitInstruction(0x00, 0, 0, 0, 0, line); return; }
    if (op == "int") { require(0); EmitInstruction(0x10, 0, 0, 0, 0, line); return; }
    if (op == "iret") {
      require(0);
      EmitInstruction(0x96, 0, 14, 0, 4, line);
      EmitInstruction(0x93, 15, 14, 0, 8, line);
      return;
    }
    if (op == "ret") { require(0); EmitInstruction(0x93, 15, 14, 0, 4, line); return; }
    if (op == "push") {
      if (args.empty() || args.size() > 3)
        Error(line, "instruction 'push' expects 1 to 3 operand(s)");


      for (const std::string& argument : args)
        EmitInstruction(0x81, 14, 0, Register(argument, line), -4, line);
      return;
    }
    if (op == "pop") { require(1); EmitInstruction(0x93, Register(args[0], line), 14, 0, 4, line); return; }
    if (op == "inc") {
      require(1);
      const int target = Register(args[0], line);


      EmitInstruction(0x91, target, target, 0, 1, line);
      return;
    }
    if (op == "not") { require(1); int r = Register(args[0], line); EmitInstruction(0x60, r, r, 0, 0, line); return; }
    if (op == "xchg") { require(2); EmitInstruction(0x40, 0, Register(args[1], line), Register(args[0], line), 0, line); return; }
    const std::map<std::string, uint8_t> binary = {
        {"add", static_cast<uint8_t>(0x50)},
        {"sub", static_cast<uint8_t>(0x51)},
        {"mul", static_cast<uint8_t>(0x52)},
        {"div", static_cast<uint8_t>(0x53)},
        {"and", static_cast<uint8_t>(0x61)},
        {"or", static_cast<uint8_t>(0x62)},
        {"xor", static_cast<uint8_t>(0x63)},
        {"shl", static_cast<uint8_t>(0x70)},
        {"shr", static_cast<uint8_t>(0x71)}};
    auto binary_it = binary.find(op);
    if (binary_it != binary.end()) {
      require(2);
      const int source = Register(args[0], line);
      const int destination = Register(args[1], line);
      EmitInstruction(binary_it->second, destination, destination, source, 0, line);
      return;
    }
    if (op == "csrrd") { require(2); EmitInstruction(0x90, Register(args[1], line), Csr(args[0], line), 0, 0, line); return; }
    if (op == "csrwr") { require(2); EmitInstruction(0x94, Csr(args[1], line), Register(args[0], line), 0, 0, line); return; }
    if (op == "call" || op == "jmp") {
      require(1);
      EmitPoolInstruction(op == "call" ? 0x21 : 0x38, 15, 0, 0, args[0], line);
      return;
    }
    if (op == "beq" || op == "bne" || op == "bgt") {
      require(3);
      uint8_t modifier = op == "beq" ? 0x39 : (op == "bne" ? 0x3A : 0x3B);
      EmitPoolInstruction(modifier, 15, Register(args[0], line),
                          Register(args[1], line), args[2], line);
      return;
    }
    if (op == "ld") { require(2); Load(args[0], Register(args[1], line), line); return; }
    if (op == "st") { require(2); Store(Register(args[0], line), args[1], line); return; }
    Error(line, "unknown instruction '" + op + "'");
  }




  MemoryAddress MemoryOperand(const std::string& operand, size_t line) const {
    if (operand.size() < 3 || operand.front() != '[' || operand.back() != ']')
      Error(line, "malformed memory operand '" + operand + "'");
    const std::string body = Trim(operand.substr(1, operand.size() - 2));
    size_t split = std::string::npos;
    for (size_t i = 1; i < body.size(); ++i)
      if (body[i] == '+' || body[i] == '-') { split = i; break; }
    MemoryAddress address;
    address.base = Register(Trim(body.substr(0, split)), line);
    if (split == std::string::npos) return address;

    const char operation = body[split];
    const std::string tail = Trim(body.substr(split + 1));
    if (tail.empty()) Error(line, "missing register displacement");
    if (operation == '+' && tail[0] == '%') {
      address.index = Register(tail, line);
    } else {
      address.displacement = std::string(1, operation) + tail;
    }
    return address;
  }



  void EmitRegisterMemory(uint8_t opmod, int a, int b, int c,
                          const std::string& expression, size_t line) {
    int64_t literal = 0;
    if (Literal(expression, &literal)) {
      if (!Fits12(literal))
        Error(line, "register displacement does not fit signed 12 bits");
      EmitInstruction(opmod, a, b, c, static_cast<int32_t>(literal), line);
      return;
    }
    const uint32_t section = RequireSection(line);
    const uint32_t instruction =
        static_cast<uint32_t>(sections_[section].section.data.size());
    EmitInstruction(opmod, a, b, c, 0, line);
    displacement_fixups_.push_back(
        DisplacementFixup{section, instruction, expression, line});
  }



  void Load(const std::string& operand, int destination, size_t line) {
    if (!operand.empty() && operand[0] == '$') {
      const std::string expression = Trim(operand.substr(1));
      int64_t value = 0;
      const bool literal = Literal(expression, &value);
      const bool direct = literal &&
          ((value >= -2048 && value <= 2047) ||
           (value >= 0xFFFFF800ll && value <= 0xFFFFFFFFll));
      if (direct) {
        EmitInstruction(0x91, destination, 0, 0, static_cast<int32_t>(value), line);
      } else {
        EmitPoolInstruction(0x92, destination, 15, 0, expression, line);
      }
      return;
    }
    if (!operand.empty() && operand[0] == '%') {
      EmitInstruction(0x91, destination, Register(operand, line), 0, 0, line);
      return;
    }
    if (!operand.empty() && operand[0] == '[') {
      const auto memory = MemoryOperand(operand, line);
      EmitRegisterMemory(0x92, destination, memory.base, memory.index,
                         memory.displacement, line);
      return;
    }
    EmitPoolInstruction(0x92, destination, 15, 0, operand, line);
    EmitInstruction(0x92, destination, destination, 0, 0, line);
  }



  void Store(int source, const std::string& operand, size_t line) {
    if (!operand.empty() && operand[0] == '$')
      Error(line, "store does not support immediate addressing");
    if (!operand.empty() && operand[0] == '%') {
      EmitInstruction(0x91, Register(operand, line), source, 0, 0, line);
      return;
    }
    if (!operand.empty() && operand[0] == '[') {
      const auto memory = MemoryOperand(operand, line);
      EmitRegisterMemory(0x80, memory.base, memory.index, source,
                         memory.displacement, line);
      return;
    }
    EmitPoolInstruction(0x82, 15, 0, source, operand, line);
  }



  Linear ResolveSymbol(const std::string& name, size_t use_line) {
    auto found = symbols_.find(name);
    if (found == symbols_.end()) Error(use_line, "undeclared symbol '" + name + "'");
    SymbolState& symbol = found->second;
    if (symbol.external || (symbol.global && !symbol.defined)) {
      Linear result;
      result.terms["@extern:" + name] = 1;
      result.external_used = true;
      return result;
    }
    if (!symbol.defined) Error(use_line, "undefined symbol '" + name + "'");
    if (symbol.expression.empty()) {
      Linear result;
      result.constant = symbol.value;
      result.terms["@section:" + std::to_string(symbol.section)] = 1;
      return result;
    }
    if (symbol.evaluating) Error(symbol.line, "circular .equ definition involving '" + name + "'");
    if (!symbol.evaluated) {
      symbol.evaluating = true;
      Linear expression;
      try {
        expression = ExpressionParser(symbol.expression).Parse();
      } catch (const std::exception& error) {
        Error(symbol.line, error.what());
      }
      Linear resolved;
      resolved.constant = expression.constant;
      resolved.external_used = expression.external_used;
      for (const auto& term : expression.terms) {
        Linear value = ResolveSymbol(term.first, symbol.line);
        resolved.constant += term.second * value.constant;
        resolved.external_used = resolved.external_used || value.external_used;
        for (const auto& coefficient : value.terms)
          resolved.terms[coefficient.first] +=
              term.second * coefficient.second;
      }
      for (auto it = resolved.terms.begin(); it != resolved.terms.end();)
        if (it->second == 0) it = resolved.terms.erase(it); else ++it;
      if (resolved.external_used)
        Error(symbol.line, ".equ symbol '" + name +
                               "' depends on an external value");
      if (resolved.constant < std::numeric_limits<int32_t>::min() ||
          resolved.constant > static_cast<int64_t>(0xFFFFFFFFull))
        Error(symbol.line, ".equ value for '" + name + "' is outside 32 bits");
      if (resolved.terms.empty()) {
        symbol.absolute = true;
        symbol.section = kSectionAbsolute;
      } else if (resolved.terms.size() == 1 &&
                 resolved.terms.begin()->second == 1 &&
                 resolved.terms.begin()->first.rfind("@section:", 0) == 0) {
        symbol.absolute = false;
        symbol.section = static_cast<uint32_t>(
            std::stoul(resolved.terms.begin()->first.substr(9)));
      } else {
        Error(symbol.line, "non-relocatable .equ expression for '" + name + "'");
      }
      symbol.value = static_cast<uint32_t>(resolved.constant);
      symbol.evaluated = true;
      symbol.evaluating = false;
    }
    Linear result;
    result.constant = symbol.value;
    if (!symbol.absolute)
      result.terms["@section:" + std::to_string(symbol.section)] = 1;
    return result;
  }




  Linear ResolveExpression(const std::string& text, size_t line) {
    Linear parsed;
    try {
      parsed = ExpressionParser(text).Parse();
    } catch (const std::exception& error) {
      Error(line, error.what());
    }
    Linear result;
    result.constant = parsed.constant;
    result.external_used = parsed.external_used;
    for (const auto& term : parsed.terms) {
      Linear symbol = ResolveSymbol(term.first, line);
      result.constant += term.second * symbol.constant;
      result.external_used = result.external_used || symbol.external_used;
      for (const auto& coefficient : symbol.terms)
        result.terms[coefficient.first] += term.second * coefficient.second;
    }
    for (auto it = result.terms.begin(); it != result.terms.end();)
      if (it->second == 0) it = result.terms.erase(it); else ++it;
    if (result.constant < std::numeric_limits<int32_t>::min() ||
        result.constant > static_cast<int64_t>(0xFFFFFFFFull))
      Error(line, "expression value is outside 32 bits");
    if (!result.terms.empty() &&
        (result.terms.size() != 1 || result.terms.begin()->second != 1))
      Error(line, "expression is not absolute or singly relocatable");
    return result;
  }



  void ResolveEquations() {
    for (const std::string& name : symbol_order_) {
      SymbolState& symbol = symbols_[name];
      if (symbol.defined && !symbol.expression.empty()) ResolveSymbol(name, symbol.line);
    }
  }



  void FlushAllPools() {
    for (uint32_t section = 0; section < sections_.size(); ++section) {
      current_section_ = section;
      FlushPool(section, 1);
    }
  }



  void ResolveDisplacements() {
    for (const DisplacementFixup& fixup : displacement_fixups_) {
      const Linear value = ResolveExpression(fixup.expression, fixup.line);
      if (!value.terms.empty())
        Error(fixup.line,
              "register displacement symbol must be absolute at assembly time");
      if (!Fits12(static_cast<int32_t>(value.constant)))
        Error(fixup.line, "register displacement does not fit signed 12 bits");
      PatchDisplacement(sections_[fixup.section], fixup.instruction_offset,
                        static_cast<int32_t>(value.constant), fixup.line);
    }
  }



  void BuildObject() {
    object_.origin = origin_;
    for (SectionState& state : sections_)
      object_.sections.push_back(std::move(state.section));

    std::vector<uint32_t> section_symbol(sections_.size());
    for (uint32_t i = 0; i < sections_.size(); ++i) {
      section_symbol[i] = static_cast<uint32_t>(object_.symbols.size());
      object_.symbols.push_back(Symbol{object_.sections[i].name, 0, i,
                                       SymbolBind::kLocal, SymbolType::kSection});
    }
    std::map<std::string, uint32_t> symbol_index;

    for (const std::string& name : symbol_order_) {
      const SymbolState& state = symbols_.at(name);
      if (!state.defined && !state.external && !state.global) continue;
      const bool undefined_global =
          !state.defined && (state.external || state.global);
      Symbol symbol;
      symbol.name = name;
      symbol.value = undefined_global ? 0 : state.value;
      symbol.section_index =
          undefined_global ? kSectionUndefined
                           : (state.absolute ? kSectionAbsolute : state.section);
      symbol.bind = (state.global || state.external)
                        ? SymbolBind::kGlobal : SymbolBind::kLocal;
      symbol.type = SymbolType::kNoType;
      symbol_index[name] = static_cast<uint32_t>(object_.symbols.size());
      object_.symbols.push_back(std::move(symbol));
    }

    for (const WordFixup& fixup : word_fixups_) {
      const Linear value = ResolveExpression(fixup.expression, fixup.line);
      if (value.terms.empty()) {
        const uint32_t word = static_cast<uint32_t>(value.constant);
        for (int i = 0; i < 4; ++i)
          object_.sections[fixup.section].data[fixup.offset + i] =
              static_cast<uint8_t>((word >> (i * 8)) & 0xFF);
        continue;
      }
      const std::string& key = value.terms.begin()->first;
      Relocation relocation;
      relocation.section_index = fixup.section;
      relocation.offset = fixup.offset;
      relocation.addend = static_cast<int32_t>(value.constant);
      if (key.rfind("@section:", 0) == 0) {
        const uint32_t section = static_cast<uint32_t>(std::stoul(key.substr(9)));
        relocation.symbol_index = section_symbol.at(section);
      } else {
        const std::string name = key.substr(8);
        relocation.symbol_index = symbol_index.at(name);
      }
      object_.relocations.push_back(relocation);
    }
  }



  std::string source_;
  std::string origin_;
  bool ended_ = false;
  uint32_t current_section_ = kSectionUndefined;
  std::vector<SectionState> sections_;
  std::map<std::string, uint32_t> section_by_name_;
  std::map<std::string, SymbolState> symbols_;
  std::vector<std::string> symbol_order_;
  std::vector<WordFixup> word_fixups_;
  std::vector<DisplacementFixup> displacement_fixups_;
  std::vector<PendingLabel> pending_labels_;
  ObjectFile object_;
};

}




ObjectFile Assemble(const std::string& source, const std::string& origin) {
#ifdef SS_USE_FLEX_BISON
  ValidateAssemblerSource(source, origin);
#endif
  return Assembler(source, origin).Run();
}

}
