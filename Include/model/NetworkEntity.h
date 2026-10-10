#pragma once

#include "common/Types.h"

namespace netmind {

class NetworkEntity {
public:
    virtual ~NetworkEntity() = default;

    virtual NodeId getId() const = 0;
};

} 