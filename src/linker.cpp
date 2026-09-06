

#include "linker.hpp"

#include <algorithm>
#include <cstdio>
#include <sstream>

#include "file_utils.hpp"

namespace ss {
namespace {


std::string Hex32(uint32_t value) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), "%08X", value);
  return buffer;
}


bool AddOverflows(uint32_t base, uint32_t size) {
  return static_cast<uint64_t>(base) + size > 0x100000000ull;
}

}


LinkerOptions ParseArguments(const std::vector<std::string>& args) {
  LinkerOptions options;
  for (size_t i = 0; i < args.size(); ++i) {
    const std::string& arg = args[i];

    if (arg == "-o") {
      if (i + 1 >= args.size()) throw LinkError("-o requires an output file name");
      if (!options.output_path.empty())
        throw LinkError("-o given more than once");
      options.output_path = args[++i];
      continue;
    }

    if (arg == "-hex" || arg == "-relocatable") {
      const OutputMode requested =
          (arg == "-hex") ? OutputMode::kHex : OutputMode::kRelocatable;
      if (options.mode_given && options.mode != requested)
        throw LinkError("-hex and -relocatable are mutually exclusive");
      options.mode = requested;
      options.mode_given = true;
      continue;
    }

    if (arg.rfind("-place=", 0) == 0) {
      const std::string body = arg.substr(7);
      const size_t at = body.find('@');
      if (at == std::string::npos || at == 0 || at + 1 == body.size())
        throw LinkError("malformed option '" + arg +
                        "', expected -place=<section>@<address>");
      const std::string section = body.substr(0, at);
      const std::string address_text = body.substr(at + 1);
      size_t consumed = 0;
      unsigned long long address = 0;
      try {
        address = std::stoull(address_text, &consumed, 0);
      } catch (const std::exception&) {
        throw LinkError("malformed address in '" + arg + "'");
      }
      if (consumed != address_text.size())
        throw LinkError("trailing characters in address of '" + arg + "'");
      if (address > 0xFFFFFFFFull)
        throw LinkError("address in '" + arg + "' does not fit in 32 bits");
      for (const auto& existing : options.placements) {
        if (existing.first == section)
          throw LinkError("section '" + section + "' is placed more than once");
      }
      options.placements.emplace_back(section, static_cast<uint32_t>(address));
      continue;
    }

    if (!arg.empty() && arg[0] == '-')
      throw LinkError("unknown option '" + arg + "'");

    options.input_paths.push_back(arg);
  }

  if (!options.mode_given)
    throw LinkError("exactly one of -hex or -relocatable must be given");
  if (options.output_path.empty())
    throw LinkError("no output file given; use -o <output_file>");
  if (options.input_paths.empty())
    throw LinkError("no input object files given");



  if (options.mode == OutputMode::kRelocatable) options.placements.clear();
  for (const std::string& input : options.input_paths) {
    if (PathsAlias(input, options.output_path) ||
        PathsAlias(input, options.output_path + ".tmp"))
      throw LinkError("input file '" + input +
                      "' is also the output file; refusing to overwrite an input");
  }
  return options;
}


void Linker::Ingest() {
  inputs_.reserve(options_.input_paths.size());
  for (const std::string& path : options_.input_paths)
    inputs_.push_back(ReadObjectFile(path));
}


void Linker::MergeSections() {


  for (size_t file_index = 0; file_index < inputs_.size(); ++file_index) {
    const ObjectFile& input = inputs_[file_index];
    for (size_t s = 0; s < input.sections.size(); ++s) {
      const Section& section = input.sections[s];
      auto it = std::find_if(sections_.begin(), sections_.end(),
                             [&](const MergedSection& m) { return m.name == section.name; });
      if (it == sections_.end()) {
        sections_.push_back(MergedSection{});
        it = sections_.end() - 1;
        it->name = section.name;
      }
      Contribution contribution;
      contribution.file_index = file_index;
      contribution.section_index = static_cast<uint32_t>(s);
      contribution.offset = static_cast<uint32_t>(it->data.size());
      contribution.size = static_cast<uint32_t>(section.data.size());
      if (AddOverflows(contribution.offset, contribution.size))
        throw LinkError("merged section '" + section.name +
                        "' exceeds the 32-bit address space");
      it->data.insert(it->data.end(), section.data.begin(), section.data.end());
      it->contributions.push_back(contribution);
    }
  }
}



void Linker::MapSections() {

  for (const auto& placement : options_.placements) {
    auto it = std::find_if(sections_.begin(), sections_.end(),
                           [&](const MergedSection& m) { return m.name == placement.first; });
    if (it == sections_.end()) {




      std::fprintf(stderr,
                   "linker: warning: -place refers to section '%s', which none "
                   "of the input files defines; ignoring it\n",
                   placement.first.c_str());
      continue;
    }
    it->base = placement.second;
    it->placed = true;
    if (AddOverflows(it->base, static_cast<uint32_t>(it->data.size())))
      throw LinkError("section '" + it->name + "' placed at 0x" + Hex32(it->base) +
                      " runs past the end of the address space");
  }



  uint64_t next = 0;
  for (const MergedSection& section : sections_) {
    if (!section.placed) continue;
    const uint64_t end = static_cast<uint64_t>(section.base) + section.data.size();
    if (end > next) next = end;
  }
  for (MergedSection& section : sections_) {
    if (section.placed) continue;
    if (next > 0xFFFFFFFFull)
      throw LinkError("section '" + section.name +
                      "' does not fit in the remaining address space");
    section.base = static_cast<uint32_t>(next);
    if (AddOverflows(section.base, static_cast<uint32_t>(section.data.size())))
      throw LinkError("section '" + section.name +
                      "' does not fit in the remaining address space");
    next += section.data.size();
  }


  for (size_t i = 0; i < sections_.size(); ++i) {
    for (size_t j = i + 1; j < sections_.size(); ++j) {
      const MergedSection& a = sections_[i];
      const MergedSection& b = sections_[j];
      if (a.data.empty() || b.data.empty()) continue;
      const uint64_t a_end = static_cast<uint64_t>(a.base) + a.data.size();
      const uint64_t b_end = static_cast<uint64_t>(b.base) + b.data.size();
      if (a.base < b_end && b.base < a_end)
        throw LinkError("sections '" + a.name + "' (0x" + Hex32(a.base) +
                        ") and '" + b.name + "' (0x" + Hex32(b.base) +
                        ") overlap");
    }
  }
}



void Linker::DetermineSymbols() {
  for (size_t file_index = 0; file_index < inputs_.size(); ++file_index) {
    const ObjectFile& input = inputs_[file_index];
    for (const Symbol& symbol : input.symbols) {
      if (symbol.type == SymbolType::kSection) continue;
      if (symbol.bind != SymbolBind::kGlobal) continue;
      if (symbol.section_index == kSectionUndefined) continue;

      auto existing = globals_.find(symbol.name);
      if (existing != globals_.end())
        throw LinkError("multiple definition of symbol '" + symbol.name +
                        "' (first in " + existing->second.defining_file +
                        ", again in " + input.origin + ")");

      ResolvedSymbol resolved;
      resolved.name = symbol.name;
      resolved.defining_file = input.origin;
      if (symbol.section_index == kSectionAbsolute) {


        resolved.absolute = true;
        resolved.value = symbol.value;
      } else {
        const Section& section = input.sections[symbol.section_index];
        auto merged = std::find_if(sections_.begin(), sections_.end(),
                                   [&](const MergedSection& m) { return m.name == section.name; });
        if (merged == sections_.end())
          throw LinkError("internal error: section '" + section.name +
                          "' was never merged");
        uint32_t contribution_offset = 0;
        bool found = false;
        for (const Contribution& c : merged->contributions) {
          if (c.file_index == file_index && c.section_index == symbol.section_index) {
            contribution_offset = c.offset;
            found = true;
            break;
          }
        }
        if (!found)
          throw LinkError("internal error: no contribution for section '" +
                          section.name + "' of " + input.origin);
        resolved.absolute = false;
        resolved.merged_section =
            static_cast<uint32_t>(merged - sections_.begin());
        resolved.value = contribution_offset + symbol.value;
      }
      globals_[symbol.name] = resolved;
    }
  }



  if (options_.mode == OutputMode::kHex) {
    for (const ObjectFile& input : inputs_) {
      for (const Symbol& symbol : input.symbols) {
        if (symbol.bind != SymbolBind::kGlobal) continue;
        if (symbol.section_index != kSectionUndefined) continue;
        if (globals_.find(symbol.name) == globals_.end())
          throw LinkError("undefined symbol '" + symbol.name + "', imported by " +
                          input.origin);
      }
    }
  }
}



uint32_t Linker::SymbolValueFor(size_t file_index,
                                const Relocation& relocation) const {
  const ObjectFile& input = inputs_[file_index];
  const Symbol& symbol = input.symbols[relocation.symbol_index];

  if (symbol.section_index == kSectionAbsolute) return symbol.value;

  if (symbol.bind == SymbolBind::kGlobal) {
    auto it = globals_.find(symbol.name);
    if (it == globals_.end())
      throw LinkError("undefined symbol '" + symbol.name + "', referenced by " +
                      input.origin);
    if (it->second.absolute) return it->second.value;
    return sections_[it->second.merged_section].base + it->second.value;
  }


  if (symbol.section_index == kSectionUndefined)
    throw LinkError("local symbol '" + symbol.name + "' in " + input.origin +
                    " is undefined");
  const Section& section = input.sections[symbol.section_index];
  auto merged = std::find_if(sections_.begin(), sections_.end(),
                             [&](const MergedSection& m) { return m.name == section.name; });
  if (merged == sections_.end())
    throw LinkError("internal error: section '" + section.name + "' was never merged");
  for (const Contribution& c : merged->contributions) {
    if (c.file_index == file_index && c.section_index == symbol.section_index)
      return merged->base + c.offset + symbol.value;
  }
  throw LinkError("internal error: no contribution for local symbol '" +
                  symbol.name + "'");
}



void Linker::ResolveRelocations() {
  for (size_t file_index = 0; file_index < inputs_.size(); ++file_index) {
    const ObjectFile& input = inputs_[file_index];
    for (const Relocation& relocation : input.relocations) {
      const Section& section = input.sections[relocation.section_index];
      auto merged = std::find_if(sections_.begin(), sections_.end(),
                                 [&](const MergedSection& m) { return m.name == section.name; });
      if (merged == sections_.end())
        throw LinkError("internal error: section '" + section.name + "' was never merged");
      uint32_t contribution_offset = 0;
      bool found = false;
      for (const Contribution& c : merged->contributions) {
        if (c.file_index == file_index && c.section_index == relocation.section_index) {
          contribution_offset = c.offset;
          found = true;
          break;
        }
      }
      if (!found)
        throw LinkError("internal error: no contribution for a relocation in " +
                        input.origin);


      const uint32_t symbol_value = SymbolValueFor(file_index, relocation);
      const uint32_t patched =
          symbol_value + static_cast<uint32_t>(relocation.addend);
      const size_t at = contribution_offset + relocation.offset;
      if (at + 4 > merged->data.size())
        throw LinkError("internal error: relocation site outside merged section '" +
                        merged->name + "'");
      for (int byte = 0; byte < 4; ++byte)
        merged->data[at + byte] = static_cast<uint8_t>((patched >> (8 * byte)) & 0xFF);
    }
  }
}



std::string Linker::EmitHex() const {
  std::vector<const MergedSection*> ordered;
  for (const MergedSection& section : sections_)
    if (!section.data.empty()) ordered.push_back(&section);
  std::sort(ordered.begin(), ordered.end(),
            [](const MergedSection* a, const MergedSection* b) { return a->base < b->base; });

  std::ostringstream out;
  char byte[4];
  for (const MergedSection* section : ordered) {
    for (size_t i = 0; i < section->data.size(); i += 8) {
      out << Hex32(section->base + static_cast<uint32_t>(i)) << ':';
      const size_t end = std::min(i + 8, section->data.size());
      for (size_t j = i; j < end; ++j) {
        std::snprintf(byte, sizeof(byte), "%02x", section->data[j]);
        out << ' ' << byte;
      }
      out << '\n';
    }
  }
  return out.str();
}



ObjectFile Linker::BuildRelocatable() const {
  ObjectFile out;
  out.origin = options_.output_path;


  for (const MergedSection& section : sections_) {
    Section copy;
    copy.name = section.name;
    copy.data = section.data;
    out.sections.push_back(std::move(copy));
  }




  std::vector<uint32_t> section_symbol(sections_.size(), 0);
  for (size_t i = 0; i < sections_.size(); ++i) {
    Symbol symbol;
    symbol.name = sections_[i].name;
    symbol.value = 0;
    symbol.section_index = static_cast<uint32_t>(i);
    symbol.bind = SymbolBind::kLocal;
    symbol.type = SymbolType::kSection;
    section_symbol[i] = static_cast<uint32_t>(out.symbols.size());
    out.symbols.push_back(std::move(symbol));
  }


  std::map<std::string, uint32_t> global_index;
  for (const auto& entry : globals_) {
    const ResolvedSymbol& resolved = entry.second;
    Symbol symbol;
    symbol.name = resolved.name;
    symbol.bind = SymbolBind::kGlobal;
    symbol.type = SymbolType::kNoType;
    if (resolved.absolute) {
      symbol.section_index = kSectionAbsolute;
      symbol.value = resolved.value;
    } else {
      symbol.section_index = resolved.merged_section;
      symbol.value = resolved.value;
    }
    global_index[resolved.name] = static_cast<uint32_t>(out.symbols.size());
    out.symbols.push_back(std::move(symbol));
  }
  for (const ObjectFile& input : inputs_) {
    for (const Symbol& symbol : input.symbols) {
      if (symbol.bind != SymbolBind::kGlobal) continue;
      if (symbol.section_index != kSectionUndefined) continue;
      if (global_index.count(symbol.name)) continue;
      Symbol import;
      import.name = symbol.name;
      import.bind = SymbolBind::kGlobal;
      import.type = SymbolType::kNoType;
      import.section_index = kSectionUndefined;
      import.value = 0;
      global_index[symbol.name] = static_cast<uint32_t>(out.symbols.size());
      out.symbols.push_back(std::move(import));
    }
  }


  for (size_t file_index = 0; file_index < inputs_.size(); ++file_index) {
    const ObjectFile& input = inputs_[file_index];
    for (const Relocation& relocation : input.relocations) {
      const Section& section = input.sections[relocation.section_index];
      auto merged = std::find_if(sections_.begin(), sections_.end(),
                                 [&](const MergedSection& m) { return m.name == section.name; });
      const uint32_t merged_index = static_cast<uint32_t>(merged - sections_.begin());
      uint32_t contribution_offset = 0;
      for (const Contribution& c : merged->contributions) {
        if (c.file_index == file_index && c.section_index == relocation.section_index) {
          contribution_offset = c.offset;
          break;
        }
      }

      Relocation out_relocation;
      out_relocation.type = RelocationType::kAbs32;
      out_relocation.section_index = merged_index;
      out_relocation.offset = contribution_offset + relocation.offset;

      const Symbol& symbol = input.symbols[relocation.symbol_index];
      if (symbol.bind == SymbolBind::kGlobal ||
          symbol.section_index == kSectionAbsolute) {
        auto it = global_index.find(symbol.name);
        if (it == global_index.end())
          throw LinkError("internal error: symbol '" + symbol.name +
                          "' missing from the output symbol table");
        out_relocation.symbol_index = it->second;
        out_relocation.addend = relocation.addend;
      } else {


        auto local_merged = std::find_if(
            sections_.begin(), sections_.end(),
            [&](const MergedSection& m) { return m.name == input.sections[symbol.section_index].name; });
        const uint32_t local_index = static_cast<uint32_t>(local_merged - sections_.begin());
        uint32_t local_offset = 0;
        for (const Contribution& c : local_merged->contributions) {
          if (c.file_index == file_index && c.section_index == symbol.section_index) {
            local_offset = c.offset;
            break;
          }
        }
        out_relocation.symbol_index = section_symbol[local_index];
        out_relocation.addend = static_cast<int32_t>(
            static_cast<uint32_t>(relocation.addend) + local_offset + symbol.value);
      }
      out.relocations.push_back(out_relocation);
    }
  }

  return out;
}

}
