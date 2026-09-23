#pragma once
#include "sm_skeleton.hpp"

namespace sm {
// Coordinate reconstruction is atomic. Public node writes outside a batch use
// constrained movement; internal solvers and pose restoration use this scope.
class geometry_batch {
    const topology& topology_;
    std::vector<std::pair<node*,point>> saved_;
    bool outer_, committed_ = false;
public:
    explicit geometry_batch(const topology&);
    ~geometry_batch();
    geometry_batch(const geometry_batch&) = delete;
    geometry_batch& operator=(const geometry_batch&) = delete;
    result commit();
};
}
