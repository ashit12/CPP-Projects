#include "matching_engine.hpp"
#include "types.hpp"
#include <iostream>

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
    orderBook.add(order);
    log(order);
  }
}

void MatchingEngine::log(const Order &order) const {
  std ::cout << "Order = " << order.price << " " << order.quantity << "\n";
}

void MatchingEngine::cancel(OrderId id) {
    orderBook.cancel(id);
}
} // namespace lob
