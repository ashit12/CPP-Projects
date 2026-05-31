#pragma once

#include <map>
#include <utility>
#include <functional>
#include <optional>
#include <unordered_map>
#include "price_level.hpp"
#include "types.hpp"

namespace lob
{

    class OrderBook
    {
    public:
        OrderId add(const Order& order);
        void cancel(OrderId id);
        std::optional<Price> getBestAsk() const;
        std::optional<Price> getBestBid() const;
    private:
        std::map<Price, PriceLevel> asks;
        std::map<Price, PriceLevel, std::greater<Price>> bids;
        std::unordered_map<OrderId, std::pair<Side, Price>> orderIndex;
        Quantity match(const Order& order);
    };
}
