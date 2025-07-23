#include <any>
#include<iostream>

struct Holder {
    virtual const std::type_info& type() = 0;
    virtual std::unique_ptr<Holder> clone() = 0;
    virtual ~Holder() = default;
};

template<typename Type>
class HolderImpl final : public Holder {
    Type value;
public:
    Type& get() {
        return value;
    }
    explicit HolderImpl(Type val) : value{val} {}
    const std::type_info &type() override {
        return typeid(value);
    }
    std::unique_ptr<Holder> clone() override {
        return std::make_unique<HolderImpl>(*this);
    }
    ~HolderImpl() override = default;
};

class Any {
    std::unique_ptr<Holder> ptr;
public:
    Any() = default;

    template<typename Type, typename = std::enable_if_t<!std::is_same_v<std::decay_t<Type>, Any>>>
    Any(Type&& val) : ptr(std::make_unique<HolderImpl<Type>>(std::forward<std::decay_t<Type>>(val))) {}

    Any(const Any& any) : ptr(any.ptr->clone()) {}
    Any(Any&& any) noexcept: ptr(std::move(any.ptr)) {}


    Any& operator=(const Any& any) {
        if (this == &any) {
            return *this;
        }
        ptr = any.ptr->clone();
        return *this;
    };
    Any& operator=(Any&& any) noexcept {
        if (this == &any) {
            return *this;
        }
        ptr = std::move(any.ptr);
        return *this;
    }

    [[nodiscard]] bool has_value() const {
        return ptr != nullptr;
    }

    [[nodiscard]] const std::type_info& type() const {
        return ptr->type();
    }

    template<typename Type>
    friend Type& any_cast(const Any& any) {
        if (!any.has_value()) {
            throw std::bad_any_cast();
        }
        auto casted = dynamic_cast<HolderImpl<Type>*>(any.ptr.get());
        if (!casted) {
            throw std::bad_any_cast();
        }
        return casted->get();
    }

    ~Any() = default;
};



int main() {
    std::cout << "=== Basic Types ===\n";
    Any a = 5;
    Any b = std::string("hello");
    Any c = 3.14;

    std::cout << any_cast<int>(a) << "\n";                // 5
    std::cout << any_cast<std::string>(b) << "\n";        // hello
    std::cout << any_cast<double>(c) << "\n";             // 3.14

    std::cout << "\n=== Copy / Move ===\n";
    Any d = a;  // copy
    Any e = std::move(b);  // move
    std::cout << any_cast<int>(d) << "\n";                // 5
    std::cout << any_cast<std::string>(e) << "\n";        // hello

    std::cout << "\n=== Type Safety (Expect Exception) ===\n";
    try {
    std::cout << any_cast<float>(c) << "\n";  // Wrong type
    } catch (const std::bad_any_cast& ex) {
    std::cout << "Caught exception: " << ex.what() << "\n";
    }

    std::cout << "\n=== Vector of Any ===\n";
    std::vector<Any> vec;
    vec.emplace_back(42);
    vec.emplace_back(std::string("world"));
    vec.emplace_back(2.718);

    std::cout << any_cast<int>(vec[0]) << "\n";           // 42
    std::cout << any_cast<std::string>(vec[1]) << "\n";   // world
    std::cout << any_cast<double>(vec[2]) << "\n";        // 2.718

    std::cout << "\n=== Type Checking ===\n";
    std::cout << (a.type() == typeid(int)) << "\n";       // 1 (true)
    if (b.has_value()) {
        std::cout << b.type().name() << "\n";
    } else {
        std::cout << "empty\n";
    }
    return 0;
}
