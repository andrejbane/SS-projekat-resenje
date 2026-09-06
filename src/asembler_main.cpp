#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "asembler.hpp"
#include "file_utils.hpp"
#include "object.hpp"

namespace {



void Usage() {
  std::cerr << "usage: assembler -o <output_file> <input_file>\n";
}

}



int main(int argc, char** argv) {
  if (argc != 4) {
    Usage();
    return 1;
  }

  std::string output;
  std::string input;
  if (std::string(argv[1]) == "-o") {
    output = argv[2];
    input = argv[3];
  } else if (std::string(argv[2]) == "-o") {
    input = argv[1];
    output = argv[3];
  } else {
    Usage();
    return 1;
  }
  if (input.empty() || output.empty() || input[0] == '-') {
    Usage();
    return 1;
  }
  const std::string temporary = output + ".tmp";
  if (ss::PathsAlias(input, output) || ss::PathsAlias(input, temporary)) {
    std::cerr << "assembler: input and output file must be different\n";
    return 1;
  }

  std::remove(output.c_str());
  std::remove(temporary.c_str());
  try {
    std::ifstream stream(input, std::ios::binary);
    if (!stream) throw ss::AssemblerError("cannot open input file '" + input + "'");
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad()) throw ss::AssemblerError("error while reading '" + input + "'");

    const ss::ObjectFile object = ss::Assemble(buffer.str(), input);
    ss::WriteObjectFile(object, temporary);
    ss::CommitTemporaryFile(temporary, output);
    return 0;
  } catch (const ss::AssemblerError& error) {
    std::cerr << "assembler: " << error.what() << '\n';
  } catch (const ss::ObjectError& error) {
    std::cerr << "assembler: " << error.what() << '\n';
  } catch (const ss::FileError& error) {
    std::cerr << "assembler: " << error.what() << '\n';
  } catch (const std::exception& error) {
    std::cerr << "assembler: unexpected failure: " << error.what() << '\n';
  }
  ss::RemoveOutputFiles(output);
  return 1;
}
