#include "fileio.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace wb {

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
