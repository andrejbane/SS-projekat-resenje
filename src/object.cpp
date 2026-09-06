

















#include "object.hpp"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace ss {
namespace {



class LineReader {
 public:

  LineReader(const std::string& text, const std::string& origin)
      : origin_(origin) {
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      lines_.push_back(line);
    }
  }



  const std::string& Next(const std::string& expected) {
    while (index_ < lines_.size()) {
      const std::string& line = lines_[index_++];
      if (!IsSkippable(line)) return line;
    }
    throw ObjectError(origin_ + ": unexpected end of file, expected " +
                      expected);
  }


  bool AtEnd() {
    while (index_ < lines_.size() && IsSkippable(lines_[index_])) ++index_;
    return index_ >= lines_.size();
  }


  size_t line_number() const { return index_; }


  ObjectError Error(const std::string& message) const {
    return ObjectError(origin_ + ":" + std::to_string(index_) + ": " + message);
  }

 private:

  static bool IsSkippable(const std::string& line) {
    for (char c : line) {
      if (c == ' ' || c == '\t') continue;
      return c == ';';
    }
    return true;
  }

  std::string origin_;
  std::vector<std::string> lines_;
  size_t index_ = 0;
};




uint32_t ParseU32(const LineReader& reader, const std::string& token,
                  const std::string& what) {
  if (token.empty()) throw reader.Error("empty " + what);
  size_t consumed = 0;
  unsigned long long value = 0;
  try {
    value = std::stoull(token, &consumed, 0);
  } catch (const std::exception&) {
    throw reader.Error("malformed " + what + ": '" + token + "'");
  }
  if (consumed != token.size())
    throw reader.Error("trailing characters in " + what + ": '" + token + "'");
  if (value > 0xFFFFFFFFull)
    throw reader.Error(what + " does not fit in 32 bits: '" + token + "'");
  return static_cast<uint32_t>(value);
}


int32_t ParseI32(const LineReader& reader, const std::string& token,
                 const std::string& what) {
  if (token.empty()) throw reader.Error("empty " + what);
  size_t consumed = 0;
  long long value = 0;
  try {
    value = std::stoll(token, &consumed, 0);
  } catch (const std::exception&) {
    throw reader.Error("malformed " + what + ": '" + token + "'");
  }
  if (consumed != token.size())
    throw reader.Error("trailing characters in " + what + ": '" + token + "'");
  if (value < -2147483648LL || value > 2147483647LL)
    throw reader.Error(what + " out of 32-bit range: '" + token + "'");
  return static_cast<int32_t>(value);
}


std::vector<std::string> Split(const std::string& line) {
  std::vector<std::string> tokens;
  std::istringstream stream(line);
  std::string token;
  while (stream >> token) tokens.push_back(token);
  return tokens;
}


uint8_t ParseHexByte(const LineReader& reader, const std::string& token) {
  if (token.size() != 2) throw reader.Error("expected a two-digit hex byte, got '" + token + "'");
  uint8_t result = 0;
  for (char c : token) {
    int digit;
    if (c >= '0' && c <= '9') digit = c - '0';
    else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    else throw reader.Error("invalid hex digit in '" + token + "'");
    result = static_cast<uint8_t>(result * 16 + digit);
  }
  return result;
}


uint32_t ParseSectionIndex(const LineReader& reader, const std::string& token,
                           size_t section_count) {
  if (token == "UND") return kSectionUndefined;
  if (token == "ABS") return kSectionAbsolute;
  uint32_t index = ParseU32(reader, token, "section index");
  if (index >= section_count)
    throw reader.Error("section index " + token + " is out of range (" +
                       std::to_string(section_count) + " sections)");
  return index;
}

}


uint32_t ObjectFile::FindSection(const std::string& name) const {
  for (size_t i = 0; i < sections.size(); ++i)
    if (sections[i].name == name) return static_cast<uint32_t>(i);
  return kSectionUndefined;
}


std::string SectionIndexToString(uint32_t index) {
  if (index == kSectionUndefined) return "UND";
  if (index == kSectionAbsolute) return "ABS";
  return std::to_string(index);
}


std::string BindToString(SymbolBind bind) {
  return bind == SymbolBind::kGlobal ? "GLOB" : "LOC";
}


std::string TypeToString(SymbolType type) {
  return type == SymbolType::kSection ? "SCTN" : "NOTYP";
}


std::string RelocationTypeToString(RelocationType) { return "ABS32"; }



ObjectFile ParseObject(const std::string& text, const std::string& origin) {
  LineReader reader(text, origin);
  ObjectFile object;
  object.origin = origin;


  {
    std::vector<std::string> header = Split(reader.Next("'#objfile 1' header"));
    if (header.size() != 2 || header[0] != "#objfile")
      throw reader.Error("expected '#objfile <version>' header");
    if (header[1] != "1")
      throw reader.Error("unsupported object format version '" + header[1] +
                         "', this build understands version 1");
  }




  bool saw_symbol_table = false;
  bool saw_relocation_table = false;
  while (!reader.AtEnd()) {
    std::vector<std::string> tokens = Split(reader.Next("a section, symbol table or relocation table"));
    if (tokens.empty()) continue;

    if (tokens[0] == "#section") {


      if (saw_symbol_table)
        throw reader.Error("section records must precede the symbol table");
      if (tokens.size() != 3)
        throw reader.Error("expected '#section <name> <byte-count>'");
      Section section;
      section.name = tokens[1];
      if (object.FindSection(section.name) != kSectionUndefined)
        throw reader.Error("duplicate section '" + section.name + "'");
      const uint32_t size = ParseU32(reader, tokens[2], "section byte count");
      section.data.reserve(size);
      while (section.data.size() < size) {
        std::vector<std::string> bytes = Split(reader.Next("section payload bytes"));
        if (bytes.empty())
          throw reader.Error("expected payload bytes for section '" + section.name + "'");
        for (const std::string& byte : bytes) {
          if (section.data.size() == size)
            throw reader.Error("section '" + section.name + "' declares " +
                               std::to_string(size) + " bytes but the payload is longer");
          section.data.push_back(ParseHexByte(reader, byte));
        }
      }
      object.sections.push_back(std::move(section));
      continue;
    }

    if (tokens[0] == "#symtab") {


      if (saw_symbol_table)
        throw reader.Error("object file contains more than one symbol table");
      if (saw_relocation_table)
        throw reader.Error("symbol table must precede the relocation table");
      saw_symbol_table = true;
      if (tokens.size() != 2) throw reader.Error("expected '#symtab <count>'");
      const uint32_t count = ParseU32(reader, tokens[1], "symbol count");
      for (uint32_t i = 0; i < count; ++i) {
        std::vector<std::string> f = Split(reader.Next("a symbol table entry"));
        if (f.size() != 6)
          throw reader.Error("expected '<num> <value> <type> <bind> <ndx> <name>'");
        const uint32_t num = ParseU32(reader, f[0], "symbol number");
        if (num != i)
          throw reader.Error("symbol numbers must be consecutive from 0; expected " +
                             std::to_string(i) + ", got " + f[0]);
        Symbol symbol;
        symbol.value = ParseU32(reader, f[1], "symbol value");
        if (f[2] == "NOTYP") symbol.type = SymbolType::kNoType;
        else if (f[2] == "SCTN") symbol.type = SymbolType::kSection;
        else throw reader.Error("unknown symbol type '" + f[2] + "'");
        if (f[3] == "LOC") symbol.bind = SymbolBind::kLocal;
        else if (f[3] == "GLOB") symbol.bind = SymbolBind::kGlobal;
        else throw reader.Error("unknown symbol binding '" + f[3] + "'");
        symbol.section_index = ParseSectionIndex(reader, f[4], object.sections.size());
        symbol.name = f[5];
        if (symbol.section_index == kSectionUndefined &&
            symbol.bind == SymbolBind::kLocal)
          throw reader.Error("symbol '" + symbol.name +
                             "' is undefined but has local binding; an imported "
                             "symbol must be GLOB");
        object.symbols.push_back(std::move(symbol));
      }
      continue;
    }

    if (tokens[0] == "#rela") {


      if (!saw_symbol_table)
        throw reader.Error("relocation table must follow the symbol table");
      if (saw_relocation_table)
        throw reader.Error("object file contains more than one relocation table");
      saw_relocation_table = true;
      if (tokens.size() != 2) throw reader.Error("expected '#rela <count>'");
      const uint32_t count = ParseU32(reader, tokens[1], "relocation count");
      for (uint32_t i = 0; i < count; ++i) {
        std::vector<std::string> f = Split(reader.Next("a relocation entry"));
        if (f.size() != 5)
          throw reader.Error("expected '<section-ndx> <offset> <type> <symbol-num> <addend>'");
        Relocation relocation;
        relocation.section_index = ParseU32(reader, f[0], "relocation section index");
        if (relocation.section_index >= object.sections.size())
          throw reader.Error("relocation refers to section index " + f[0] +
                             " but only " + std::to_string(object.sections.size()) +
                             " sections are defined");
        relocation.offset = ParseU32(reader, f[1], "relocation offset");
        if (f[2] != "ABS32")
          throw reader.Error("unknown relocation type '" + f[2] +
                             "'; this architecture defines only ABS32");
        relocation.type = RelocationType::kAbs32;
        relocation.symbol_index = ParseU32(reader, f[3], "relocation symbol number");
        if (relocation.symbol_index >= object.symbols.size())
          throw reader.Error("relocation refers to symbol " + f[3] +
                             " but only " + std::to_string(object.symbols.size()) +
                             " symbols are defined (the symbol table must precede "
                             "the relocation table)");
        relocation.addend = ParseI32(reader, f[4], "relocation addend");
        const Section& target = object.sections[relocation.section_index];
        if (relocation.offset + 4u > target.data.size() ||
            relocation.offset + 4u < relocation.offset)
          throw reader.Error("ABS32 relocation at offset " + f[1] +
                             " does not fit inside section '" + target.name +
                             "' (" + std::to_string(target.data.size()) + " bytes)");
        object.relocations.push_back(relocation);
      }
      continue;
    }

    throw reader.Error("unexpected directive '" + tokens[0] + "'");
  }

  if (!saw_symbol_table)
    throw ObjectError(origin + ": unexpected end of file, expected symbol table");
  if (!saw_relocation_table)
    throw ObjectError(origin + ": unexpected end of file, expected relocation table");

  return object;
}


ObjectFile ReadObjectFile(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) throw ObjectError("cannot open input file '" + path + "'");
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  if (stream.bad()) throw ObjectError("error while reading '" + path + "'");
  return ParseObject(buffer.str(), path);
}


std::string WriteObject(const ObjectFile& object) {
  std::ostringstream out;
  out << "#objfile 1\n";

  for (const Section& section : object.sections) {
    out << "#section " << section.name << ' ' << section.data.size() << '\n';
    char byte[4];
    for (size_t i = 0; i < section.data.size(); ++i) {
      std::snprintf(byte, sizeof(byte), "%02x", section.data[i]);
      out << byte;
      const bool row_end = (i % 8 == 7) || (i + 1 == section.data.size());
      out << (row_end ? '\n' : ' ');
    }
  }

  out << "#symtab " << object.symbols.size() << '\n';
  for (size_t i = 0; i < object.symbols.size(); ++i) {
    const Symbol& symbol = object.symbols[i];
    out << i << ' ' << symbol.value << ' ' << TypeToString(symbol.type) << ' '
        << BindToString(symbol.bind) << ' '
        << SectionIndexToString(symbol.section_index) << ' ' << symbol.name
        << '\n';
  }

  out << "#rela " << object.relocations.size() << '\n';
  for (const Relocation& relocation : object.relocations) {
    out << relocation.section_index << ' ' << relocation.offset << ' '
        << RelocationTypeToString(relocation.type) << ' '
        << relocation.symbol_index << ' ' << relocation.addend << '\n';
  }

  return out.str();
}


void WriteObjectFile(const ObjectFile& object, const std::string& path) {
  std::ofstream stream(path, std::ios::binary);
  if (!stream) throw ObjectError("cannot open output file '" + path + "'");
  stream << WriteObject(object);
  stream.flush();
  if (!stream) throw ObjectError("error while writing '" + path + "'");
}

}
