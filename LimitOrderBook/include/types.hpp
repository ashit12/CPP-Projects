#pragma once

#include <cstdint>

namespace lob {

using Price    = std::int64_t;
using Timestamp = std::uint64_t;
using Quantity = std::uint32_t;
using OrderId  = std::uint32_t;


enum class Side : std::uint8_t {
    Buy,
    Sell,
};

enum class CommandType : std::uint8_t {
    Add,
    Cancel,
};

struct Order
{
    Price price;
    Timestamp timestamp;
    Quantity quantity;
    OrderId id;
    Side side;
};

struct Command
{
    CommandType type;
    Order order;
};


} // namespace lob
