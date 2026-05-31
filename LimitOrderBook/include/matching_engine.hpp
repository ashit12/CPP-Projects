#pragma once

#include <chrono>
#include "types.hpp"
#include "order_book.hpp"

namespace lob {

class MatchingEngine {
    public:
        MatchingEngine() : nextId{0} {}
        void submit(Price price, Quantity quantity, Side side);
        void cancel(OrderId id);
    private:
        OrderBook orderBook;
        OrderId nextId;
        OrderId generateId()
        {
            return nextId++;
        }
        Timestamp getTimestamp();
        bool isValid(const Order& order) const;
        void log(const Order& order) const;
};

} // namespace lob
