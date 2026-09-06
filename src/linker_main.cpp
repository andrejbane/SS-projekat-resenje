











#include <cstdio>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

#include "file_utils.hpp"
#include "linker.hpp"
#include "object.hpp"

namespace {


void PrintUsage() {
  std::cerr
      << "usage: linker [options] <input_file>...\n"
         "  -o <output_file>           write the result here (required)\n"
         "  -hex                       emit the linked memory image\n"
         "  -relocatable               emit a merged relocatable object\n"
         "  -place=<section>@<address> place a section at a fixed address\n"
         "Exactly one of -hex or -relocatable must be given.\n";
}



void RemoveOutputAfterParseFailure(const std::vector<std::string>& args) {
  std::string output;
  size_t output_count = 0;
  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] != "-o") continue;
    ++output_count;
    if (i + 1 < args.size()) output = args[++i];
  }
  if (output_count != 1 || output.empty()) return;

  for (size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "-o") {
      ++i;
      continue;
    }
    if (!args[i].empty() && args[i][0] != '-' &&
        (ss::PathsAlias(args[i], output) ||
         ss::PathsAlias(args[i], output + ".tmp")))
      return;
  }
  ss::RemoveOutputFiles(output);
}

}



int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);
  if (args.empty()) {
    PrintUsage();
    return 1;
  }




  ss::LinkerOptions options;
  try {
    options = ss::ParseArguments(args);
  } catch (const ss::LinkError& error) {
    RemoveOutputAfterParseFailure(args);
    std::cerr << "linker: " << error.what() << '\n';
    return 1;
  }

  try {
    ss::Linker linker(options);

    linker.Ingest();
    linker.MergeSections();

    if (options.mode == ss::OutputMode::kRelocatable) {


      linker.DetermineSymbols();
      ss::WriteTextFileAtomically(ss::WriteObject(linker.BuildRelocatable()),
                                  options.output_path);
      return 0;
    }



    linker.MapSections();
    linker.DetermineSymbols();
    linker.ResolveRelocations();
    ss::WriteTextFileAtomically(linker.EmitHex(), options.output_path);
    return 0;
  } catch (const std::exception& error) {


    ss::RemoveOutputFiles(options.output_path);
    if (dynamic_cast<const ss::LinkError*>(&error) != nullptr ||
        dynamic_cast<const ss::ObjectError*>(&error) != nullptr ||
        dynamic_cast<const ss::FileError*>(&error) != nullptr) {
      std::cerr << "linker: " << error.what() << '\n';
    } else {
      std::cerr << "linker: unexpected failure: " << error.what() << '\n';
    }
    return 1;
  }
}
