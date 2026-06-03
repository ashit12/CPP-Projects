#pragma once

#include <atomic>
#include <optional>
#include "spsc_queue.hpp"
#include "types.hpp"
#include "order_book.hpp"

namespace lob {

class MatchingEngine {
    public:
        MatchingEngine() : nextId{0} {}
        void submit(Price price, Quantity quantity, Side side);
        void cancel(OrderId id);
        void run();
        void stop();
        std::optional<Price> getBestAsk() const;
        std::optional<Price> getBestBid() const;
    private:
        std::atomic<bool> running{false};
        std::atomic<Price> publishedBestAsk{0};
        std::atomic<Price> publishedBestBid{0};
        SPSCQueue<Command, 4096> queue;
        OrderBook orderBook;
        std::atomic<OrderId> nextId;
        OrderId generateId()
        {
            return nextId.fetch_add(1, std::memory_order_relaxed);
        }
        Timestamp getTimestamp();
        bool isValid(const Order& order) const;
};

} // namespace lob
