#include <utility>
#include <cassert>
#include "price_level.hpp"
#include "types.hpp"

void lob::PriceLevel::add(const Order& order)
{
    orders.push_back(order);
    orderMap[order.id] = std::prev(orders.end());
}

bool lob::PriceLevel::cancel(OrderId id)
{
    if(orderMap.contains(id))
    {
        orders.erase(orderMap[id]);
        orderMap.erase(id);
        return true;
    }
    return false;
}

void lob::PriceLevel::modifyFrontQuantity(Quantity qty)
{
    orders.front().quantity -= qty;
}

bool lob::PriceLevel::isEmpty() const
{
    return orders.empty();
}

lob::Order lob::PriceLevel::pop()
{
    assert(!isEmpty());
    auto order = std::move(orders.front());
    orders.pop_front();
    orderMap.erase(order.id);
    return order;
}

const lob::Order& lob::PriceLevel::peek() const
{
    return orders.front();
}

size_t lob::PriceLevel::size() const 
{
    return orders.size();
}
