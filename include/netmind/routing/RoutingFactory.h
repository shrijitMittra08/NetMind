#pragma once

#include "netmind/routing/RoutingAlgorithm.h"
#include "netmind/routing/WeightPolicy.h"

#include <memory>
#include <string>

namespace netmind {

class RoutingFactory {
public:
    [[nodiscard]] static std::unique_ptr<RoutingAlgorithm>
    makeAlgorithm(const std::string& name);

    [[nodiscard]] static std::unique_ptr<WeightPolicy>
    makePolicy(const std::string& name);
};

}