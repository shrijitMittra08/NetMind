
#pragma once

#include <cstdint>

namespace netmind {

using EntityId = std::uint32_t;

class NetworkEntity {
public:
    virtual ~NetworkEntity() = default;

    [[nodiscard]] virtual EntityId getId() const = 0;
};

}
