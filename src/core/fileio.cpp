#include "fileio.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "parse_util.h"

#include "parse_util.h"

namespace wb {

std::string readTextFile(const std::string& path, std::uint64_t max_bytes) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const std::uintmax_t size = fs::file_size(fs::path(path), ec);
    if (ec) throw ParseError("Cannot open '" + path + "'. Check that the file exists and is readable.");
    checkFileSize(size, max_bytes);
    std::ifstream in(fs::path(path), std::ios::binary);
    if (!in) throw ParseError("Cannot open '" + path + "'. Check that the file is not locked by another program.");
    std::string text(static_cast<size_t>(size), ' ');
    in.read(text.data(), static_cast<std::streamsize>(size));
    if (!in) throw ParseError("Reading '" + path + "' failed part-way. Try copying the file locally and retry.");
    return text;
}

void writeFileAtomic(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    const fs::path target(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    const fs::path tmp(path + ".tmp");
    {
        std::ofstream out(tmp, std::ios::binary);
        if (!out) throw std::runtime_error("Cannot write " + path + ". Check that the folder is writable.");
        out << content;
        out.flush();
        if (!out) throw std::runtime_error("Write failed for " + path + ". Check free disk space.");
    }
    fs::rename(tmp, target, ec);  // replaces an existing file on Windows
    if (ec) {
        fs::remove(tmp, ec);
        throw std::runtime_error("Could not finalise " + path + ".");
    }
}

}  // namespace wb
