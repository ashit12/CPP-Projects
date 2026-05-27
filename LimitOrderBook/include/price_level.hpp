#pragma once

#include <cstddef>
#include <unordered_map>
#include <list>
#include "types.hpp"

namespace lob
{

    class PriceLevel
    {
        public:
            void add(const Order &order);
            bool cancel(OrderId id);
            void modifyFrontQuantity(Quantity qty);
            Order pop();
            const Order &peek() const;
            bool isEmpty() const;
            size_t size() const;
        private:
            std::unordered_map<OrderId, std::list<Order>::iterator> orderMap;
            std::list<Order> orders;
    };

} // namespace lob
