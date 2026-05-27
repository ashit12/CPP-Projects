#include "order_book.hpp"
#include "types.hpp"

lob::Quantity lob::OrderBook::match(const lob::Order &order)
{
    auto orderQuantity = order.quantity;
    if (order.side == lob::Side::Buy)
    {
        for (auto it = asks.begin(); it != asks.end(); it++)
        {
            auto &[price, priceLevel] = *it;
            if (price <= order.price)
            {
                if (orderQuantity < priceLevel.peek().quantity)
                {
                    priceLevel.modifyFrontQuantity(orderQuantity);
                    return order.quantity;
                }
                else if (orderQuantity == priceLevel.peek().quantity)
                {
                    priceLevel.pop();
                    return order.quantity;
                }
                else
                {
                    auto frontOrder = priceLevel.pop();
                    orderQuantity -= frontOrder.quantity;
                    it = asks.erase(it);
                    if (it == asks.end())
                        break;
                    --it;
                }
            }
        }
        return orderQuantity;
    }
    else
    {
        for (auto it = bids.begin(); it != bids.end(); it++)
        {
            auto &[price, priceLevel] = *it;
            if (price >= order.price)
            {
                if (orderQuantity < priceLevel.peek().quantity)
                {
                    priceLevel.modifyFrontQuantity(orderQuantity);
                    return order.quantity;
                }
                else if (orderQuantity == priceLevel.peek().quantity)
                {
                    priceLevel.pop();
                    return order.quantity;
                }
                else
                {
                    auto frontOrder = priceLevel.pop();
                    orderQuantity -= frontOrder.quantity;
                    it = bids.erase(it);
                    if (it == bids.end())
                        break;
                    --it;
                }
            }
        }
        return orderQuantity;
    }
}

