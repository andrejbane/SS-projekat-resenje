








#ifndef SS_OBJECT_HPP_
#define SS_OBJECT_HPP_

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace ss {


enum : uint32_t {
  kSectionUndefined = 0xFFFFFFFFu,
  kSectionAbsolute = 0xFFFFFFFEu
};

enum class SymbolBind { kLocal, kGlobal };
enum class SymbolType { kNoType, kSection };



enum class RelocationType { kAbs32 };



class ObjectError : public std::runtime_error {
 public:

  explicit ObjectError(const std::string& message)
      : std::runtime_error(message) {}
};


struct Section {
  std::string name;
  std::vector<uint8_t> data;
};


struct Symbol {
  std::string name;
  uint32_t value = 0;

  uint32_t section_index = kSectionUndefined;
  SymbolBind bind = SymbolBind::kLocal;
  SymbolType type = SymbolType::kNoType;
};


struct Relocation {
  uint32_t section_index = 0;
  uint32_t offset = 0;
  uint32_t symbol_index = 0;
  int32_t addend = 0;
  RelocationType type = RelocationType::kAbs32;
};



struct ObjectFile {
  std::string origin;
  std::vector<Section> sections;
  std::vector<Symbol> symbols;
  std::vector<Relocation> relocations;



  uint32_t FindSection(const std::string& name) const;
};



ObjectFile ParseObject(const std::string& text, const std::string& origin);

ObjectFile ReadObjectFile(const std::string& path);



std::string WriteObject(const ObjectFile& object);

void WriteObjectFile(const ObjectFile& object, const std::string& path);



std::string SectionIndexToString(uint32_t index);

std::string BindToString(SymbolBind bind);

std::string TypeToString(SymbolType type);

std::string RelocationTypeToString(RelocationType type);

}

#endif
