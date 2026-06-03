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
          auto frontOrder = priceLevel.pop();
          filled += orderQuantity;
          if (priceLevel.isEmpty()) {
            it = asks.erase(it);
          }
          orderIndex.erase(frontOrder.id);
          break;
        } else {
          auto frontOrder = priceLevel.pop();
          filled += frontOrder.quantity;
          orderQuantity -= frontOrder.quantity;
          orderIndex.erase(frontOrder.id);
          if (priceLevel.isEmpty()) {
            it = asks.erase(it);
            if (it == asks.end())
              break;
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
          auto frontOrder = priceLevel.pop();
          orderIndex.erase(frontOrder.id);
          filled += orderQuantity;
          if (priceLevel.isEmpty()) {
            it = bids.erase(it);
          }
          break;
        } else {
          auto frontOrder = priceLevel.pop();
          filled += frontOrder.quantity;
          orderQuantity -= frontOrder.quantity;
          orderIndex.erase(frontOrder.id);
          if (priceLevel.isEmpty()) {
            it = bids.erase(it);
            if (it == bids.end())
              break;
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
  auto order = orderIndex.find(id);

  if(order != orderIndex.end())
  {
    auto [side, price] = order->second;
    if (side == Side::Sell) {
      auto level = asks.find(price);
      level->second.cancel(id);
      if (level->second.isEmpty())
        asks.erase(level);
    } else {
      auto level = bids.find(price);
      level->second.cancel(id);
      if (level->second.isEmpty())
        bids.erase(level);
    }
    orderIndex.erase(order);
  }
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
