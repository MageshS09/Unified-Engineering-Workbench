// Helpers shared by the API route files (JSON transport only).
#pragma once
#include <stdexcept>
#include <string>

#include "httplib.h"
#include "json.hpp"

namespace api {

using json = nlohmann::json;

inline void sendError(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(json{{"error", message}}.dump(), "application/json");
}

inline double numberField(const json& j, const char* key, double fallback) {
    if (!j.contains(key)) return fallback;
    if (!j[key].is_number()) throw std::invalid_argument(std::string("Field '") + key + "' must be a number.");
    return j[key].get<double>();
}

inline int intField(const json& j, const char* key, int fallback) {
    if (!j.contains(key)) return fallback;
    if (!j[key].is_number_integer()) throw std::invalid_argument(std::string("Field '") + key + "' must be a whole number.");
    return j[key].get<int>();
}

}  // namespace api
