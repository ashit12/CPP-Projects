#include "order_book.hpp"
#include "types.hpp"
#include <iostream>
#include <ostream>

namespace lob {

Quantity OrderBook::match(const Order &order) {
  auto orderQuantity = order.quantity;
  Quantity filled = 0;
  if (order.side == Side::Buy) {
    auto it = asks.begin();
    while (it != asks.end()) {
      auto &[price, priceLevel] = *it;
      if (price <= order.price) {
        if (orderQuantity < priceLevel.peek().quantity) {
          priceLevel.modifyFrontQuantity(orderQuantity);
          filled += orderQuantity;
          break;
        } else if (orderQuantity == priceLevel.peek().quantity) {
          priceLevel.pop();
          filled += orderQuantity;
          if (priceLevel.isEmpty()) {
            it = asks.erase(it);
          }
          break;
        } else {
          auto frontOrder = priceLevel.pop();
          filled += frontOrder.quantity;
          orderQuantity -= frontOrder.quantity;
          if (priceLevel.isEmpty()) {
            it = asks.erase(it);
            if (it == asks.end())
              break;
          } else {
            it++;
          }
        }
      } else {
        break;
      }
    }
  } else {
    auto it = bids.begin();
    while (it != bids.end()) {
      auto &[price, priceLevel] = *it;
      if (price >= order.price) {
        if (orderQuantity < priceLevel.peek().quantity) {
          priceLevel.modifyFrontQuantity(orderQuantity);
          filled += orderQuantity;
          break;
        } else if (orderQuantity == priceLevel.peek().quantity) {
          priceLevel.pop();
          filled += orderQuantity;
          if (priceLevel.isEmpty()) {
            it = bids.erase(it);
          }
          break;
        } else {
          auto frontOrder = priceLevel.pop();
          filled += frontOrder.quantity;
          orderQuantity -= frontOrder.quantity;
          if (priceLevel.isEmpty()) {
            it = bids.erase(it);
            if (it == bids.end())
              break;
          } else {
            it++;
          }
        }
      } else {
        break;
      }
    }
  }
  return filled;
}

OrderId OrderBook::add(const Order &order) {
  Quantity remainder = order.quantity - match(order);
  if (remainder > 0) {
    if (order.side == Side::Sell)
      asks[order.price].add(order);
    else
      bids[order.price].add(order);

    orderIndex[order.id] = {order.side, order.price};
  }
  return order.id;
}

void OrderBook::cancel(OrderId id) {
  if (!orderIndex.contains(id))
    return;

  auto [side, price] = orderIndex[id];
  if (side == Side::Sell) {
    asks[price].cancel(id);
    if (asks[price].isEmpty())
      asks.erase(price);
  } else {
    bids[price].cancel(id);
    if (bids[price].isEmpty())
      bids.erase(price);
  }
  orderIndex.erase(id);
}

std::optional<Price> OrderBook::getBestAsk() const {
  if (asks.empty())
    return std::nullopt;
  return asks.begin()->first;
}

std::optional<Price> OrderBook::getBestBid() const {
  if (bids.empty())
    return std::nullopt;
  return bids.begin()->first;
}

} // namespace lob
