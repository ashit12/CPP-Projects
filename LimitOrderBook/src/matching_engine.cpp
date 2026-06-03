#include "matching_engine.hpp"
#include "types.hpp"
#include <atomic>
#include <iostream>
#include <optional>

namespace lob {
bool MatchingEngine::isValid(const Order &order) const {
  return order.price > 0 && order.quantity > 0;
}

Timestamp MatchingEngine::getTimestamp() {
  auto now = std::chrono::steady_clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             now.time_since_epoch())
      .count();
}

void MatchingEngine::submit(Price price, Quantity quantity, Side side) {
  Order order{.price = price,
              .timestamp = getTimestamp(),
              .quantity = quantity,
              .id = generateId(),
              .side = side};
  if (isValid(order)) {
    Command command{.type = CommandType::Add, .order = order};
    queue.push(command);
  }
}

void MatchingEngine::run() {
  running = true;
  while (running.load(std::memory_order_relaxed)) {
    auto item = queue.pop();
    if (item.has_value()) {
      auto &cmd = *item;
      switch (cmd.type) {
      case CommandType::Add:
        orderBook.add(cmd.order);
        break;
      case CommandType::Cancel:
        orderBook.cancel(cmd.order.id);
        break;
      }
      auto bestAsk = orderBook.getBestAsk();
      auto bestBid = orderBook.getBestBid();
      publishedBestAsk.store(bestAsk.value_or(0), std::memory_order::release);
      publishedBestBid.store(bestBid.value_or(0), std::memory_order::release);
    }
  }
}

void MatchingEngine::stop() { running.store(false, std::memory_order_relaxed); }

void MatchingEngine::cancel(OrderId id) {
  Command command{.type = CommandType::Cancel, .order{.id = id}};
  queue.push(command);
}

std::optional<Price> MatchingEngine::getBestAsk() const {
  auto bestAsk = publishedBestAsk.load(std::memory_order_acquire);
  if (bestAsk == 0) {
    return std::nullopt;
  }
  return bestAsk;
}

std::optional<Price> MatchingEngine::getBestBid() const {
  auto bestBid = publishedBestBid.load(std::memory_order_acquire);
  if (bestBid == 0) {
    return std::nullopt;
  }
  return bestBid;
}

} // namespace lob
