









#ifndef SS_LINKER_HPP_
#define SS_LINKER_HPP_

#include <map>
#include <string>
#include <vector>

#include "object.hpp"

namespace ss {

class LinkError : public std::runtime_error {
 public:

  explicit LinkError(const std::string& message)
      : std::runtime_error(message) {}
};

enum class OutputMode { kHex, kRelocatable };



struct LinkerOptions {
  OutputMode mode = OutputMode::kHex;
  bool mode_given = false;
  std::string output_path;
  std::vector<std::string> input_paths;

  std::vector<std::pair<std::string, uint32_t>> placements;
};



LinkerOptions ParseArguments(const std::vector<std::string>& args);



struct Contribution {
  size_t file_index = 0;
  uint32_t section_index = 0;
  uint32_t offset = 0;
  uint32_t size = 0;
};



struct MergedSection {
  std::string name;
  uint32_t base = 0;
  bool placed = false;
  std::vector<uint8_t> data;
  std::vector<Contribution> contributions;
};



struct ResolvedSymbol {
  std::string name;
  uint32_t value = 0;
  bool absolute = false;
  uint32_t merged_section = 0;
  std::string defining_file;
};

class Linker {
 public:

  explicit Linker(const LinkerOptions& options) : options_(options) {}


  void Ingest();


  void MergeSections();



  void MapSections();


  void DetermineSymbols();


  void ResolveRelocations();


  std::string EmitHex() const;



  ObjectFile BuildRelocatable() const;


  const std::vector<MergedSection>& sections() const { return sections_; }

 private:

  uint32_t SymbolValueFor(size_t file_index, const Relocation& relocation) const;

  LinkerOptions options_;
  std::vector<ObjectFile> inputs_;
  std::vector<MergedSection> sections_;
  std::map<std::string, ResolvedSymbol> globals_;
};

}

#endif
