#include "price_level.hpp"
#include <cassert>
#include <utility>

namespace lob {

void PriceLevel::add(const Order &order) {
  orders.push_back(order);
  orderMap[order.id] = std::prev(orders.end());
}

bool PriceLevel::cancel(OrderId id) {
  auto it = orderMap.find(id);
  if (it != orderMap.end()) {
    orders.erase(it->second);
    orderMap.erase(it);
    return true;
  }
  return false;
}

void PriceLevel::modifyFrontQuantity(Quantity qty) {
  orders.front().quantity -= qty;
}

Order PriceLevel::pop() {
  assert(!isEmpty());
  auto order = std::move(orders.front());
  orders.pop_front();
  orderMap.erase(order.id);
  return order;
}

const Order &PriceLevel::peek() const {
  return orders.front();
}

bool PriceLevel::isEmpty() const { return orders.empty(); }

size_t PriceLevel::size() const { return orders.size(); }

} // namespace lob
