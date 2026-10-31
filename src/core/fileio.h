// Safe file output: write to a temp file, then rename, so readers never see half-written results.
#pragma once
#include <string>

namespace wb {

// Throws std::runtime_error with the path and reason if the write fails.
void writeFileAtomic(const std::string& path, const std::string& content);

}  // namespace wb
