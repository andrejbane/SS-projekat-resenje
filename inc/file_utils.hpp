#ifndef SS_FILE_UTILS_HPP_
#define SS_FILE_UTILS_HPP_

#include <stdexcept>
#include <string>

namespace ss {

class FileError : public std::runtime_error {
 public:

  explicit FileError(const std::string& message) : std::runtime_error(message) {}
};


bool PathsAlias(const std::string& left, const std::string& right);

void RemoveOutputFiles(const std::string& output_path);

void CommitTemporaryFile(const std::string& temporary_path,
                         const std::string& output_path);

void WriteTextFileAtomically(const std::string& text,
                             const std::string& output_path);

}

#endif
