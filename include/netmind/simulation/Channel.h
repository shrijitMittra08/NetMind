#pragma once

#include "netmind/common/Types.h"
#include "netmind/model/Packet.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <queue>
#include <vector>

namespace netmind {

class Channel {
public:
    Channel(ChannelId id, NodeId from, NodeId to);

    [[nodiscard]] ChannelId id() const;
    [[nodiscard]] LinkId linkId() const;
    [[nodiscard]] NodeId from() const;
    [[nodiscard]] NodeId to() const;

    [[nodiscard]] bool busy() const;
    [[nodiscard]] std::size_t queueSize() const;
    [[nodiscard]] bool empty() const;

                                                                    
    bool tryEnqueue(Packet packet, std::size_t capacity);

    [[nodiscard]] const Packet& front() const;

                                                                        
                                              
    std::uint64_t beginTransmission();

                                                                         
    [[nodiscard]] std::optional<Packet> completeTransmission(
        std::uint64_t generation
    );

                                                                            
    [[nodiscard]] std::vector<Packet> flush();

private:
    ChannelId id_;
    NodeId from_;
    NodeId to_;
    bool busy_{false};
    std::uint64_t generation_{0};
    std::queue<Packet> queue_;
};

}