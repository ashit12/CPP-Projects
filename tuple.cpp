#include <iostream>

template <typename... T>
class Tuple;

template<typename First, typename... Remaining>
class Tuple<First, Remaining...> {
    First first;
    Tuple<Remaining...> next;
public:
    explicit Tuple(First&& first_parameter, Remaining&&... remaining) : first{std::forward<First>(first_parameter)},
                                next(std::forward<Remaining>(remaining)...) {}

    Tuple<Remaining...>& getNext(){
        return next;
    }
    [[nodiscard]] const Tuple<Remaining...>& getNext() const {
        return next;
    }

    First& getFirst() {
        return first;
    }
    [[nodiscard]] const First& getFirst() const {
        return first;
    }
};

template <>
class Tuple<> {
public:
    Tuple() = default;
};

template<int N, typename ...T>
decltype(auto) get(Tuple<T...>& tuple) {
    if constexpr (N == 0)
        return tuple.getFirst();
    else
        return get<N - 1>(tuple.getNext());
}

int main() {
    Tuple<int, double, int> tuple(42, 2.45, 30);
    std::cout << get<3>(tuple);
    return 0;
}
