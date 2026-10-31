// Registration entry points for the HTTP API (transport layer over wb_core).
#pragma once
#include <filesystem>

#include "httplib.h"
#include "jobs.h"

namespace api {

void registerComputeRoutes(httplib::Server& svr, wb::JobManager& jobs);
void registerDeviceRoutes(httplib::Server& svr);
// data_dir holds the packaged sample files (cube.stl, synthetic_temperature.csv); may not exist.
void registerGeometryRoutes(httplib::Server& svr, const std::filesystem::path& data_dir);

}  // namespace api
