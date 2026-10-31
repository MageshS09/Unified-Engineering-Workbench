// Safe file output: write to a temp file, then rename, so readers never see half-written results.
#pragma once
#include <cstdint>
#include <string>

namespace wb {

// Reads a whole file. Throws wb::ParseError if it is missing, unreadable or larger than max_bytes.
std::string readTextFile(const std::string& path, std::uint64_t max_bytes);

// Throws std::runtime_error with the path and reason if the write fails.
void writeFileAtomic(const std::string& path, const std::string& content);

}  // namespace wb
