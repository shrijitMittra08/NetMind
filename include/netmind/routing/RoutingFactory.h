#pragma once

#include <memory>
#include <string>

#include "netmind/routing/RoutingAlgorithm.h"
#include "netmind/routing/WeightPolicy.h"

namespace netmind {

class RoutingFactory {
public:
    [[nodiscard]] static std::unique_ptr<RoutingAlgorithm> makeAlgorithm(const std::string& name);
    [[nodiscard]] static std::unique_ptr<WeightPolicy> makePolicy(const std::string& name);
};

}
