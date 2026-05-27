#pragma once

#include <map>
#include <utility>
#include <functional>
#include <unordered_map>
#include "price_level.hpp"
#include "types.hpp"

namespace lob
{

    class OrderBook
    {
    public:
        void add(OrderId id, Side side, Price price, Quantity qty);
        void cancel(OrderId id);
    private:
        std::map<Price, PriceLevel> asks;
        std::map<Price, PriceLevel, std::greater<Price>> bids;
        std::unordered_map<OrderId, std::pair<Side, Price>> orderIndex;
        Quantity match(const Order& order);
    };

}
