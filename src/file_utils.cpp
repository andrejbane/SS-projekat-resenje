#include "file_utils.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace ss {



bool PathsAlias(const std::string& left, const std::string& right) {
  namespace fs = std::filesystem;
  std::error_code error;
  const fs::path normalized_left = fs::absolute(left, error).lexically_normal();
  if (error) return left == right;
  const fs::path normalized_right = fs::absolute(right, error).lexically_normal();
  if (error) return left == right;
  if (normalized_left == normalized_right) return true;
  error.clear();
  return fs::equivalent(normalized_left, normalized_right, error) && !error;
}


void RemoveOutputFiles(const std::string& output_path) {
  std::remove((output_path + ".tmp").c_str());
  std::remove(output_path.c_str());
}



void CommitTemporaryFile(const std::string& temporary_path,
                         const std::string& output_path) {
  std::remove(output_path.c_str());
  if (std::rename(temporary_path.c_str(), output_path.c_str()) != 0) {
    std::remove(temporary_path.c_str());
    throw FileError("cannot replace output file '" + output_path + "'");
  }
}



void WriteTextFileAtomically(const std::string& text,
                             const std::string& output_path) {
  const std::string temporary = output_path + ".tmp";
  std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
  if (!stream)
    throw FileError("cannot open output file '" + output_path + "'");
  stream.write(text.data(), static_cast<std::streamsize>(text.size()));
  stream.close();
  if (!stream) {
    std::remove(temporary.c_str());
    throw FileError("error while writing '" + output_path + "'");
  }
  CommitTemporaryFile(temporary, output_path);
}

}
