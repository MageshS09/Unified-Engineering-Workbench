// Mesh-refinement study for Task C, shared by the CLI and the server.
#pragma once
#include <functional>
#include <vector>

#include "fea1d.h"

namespace wb {

struct FeaStudy {
    RodParams params;  // `elements` is ignored; each mesh comes from `solutions`
    std::vector<FeaSolution> solutions;
    std::vector<ConvergenceRow> rows;
};

// `onStep(done, total)` is called after each mesh; it may throw (e.g. JobCancelled) to abort.
FeaStudy runStudy(const RodParams& p, const std::vector<int>& meshes,
                  const std::function<void(int, int)>& onStep = {});

std::string temperatureSvg(const FeaStudy& s);  // T vs x, every mesh plus exact
std::string convergenceSvg(const FeaStudy& s);  // log-log midpoint error vs h

}  // namespace wb
