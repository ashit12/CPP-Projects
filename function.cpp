#include <iostream>

/**
 * @tparam ReturnType - denotes the return type of the function being passed in
 * @tparam Args - parameter pack that denotes the arguments being passed into the function
 */
template<typename ReturnType, typename ...Args>
struct CallableHolder {
    virtual ReturnType operator()(Args&&... args) = 0;
    virtual std::unique_ptr<CallableHolder> clone() = 0;
    virtual ~CallableHolder() = default;
};

template<typename Callable, typename ReturnType, typename... Args>
class CallableHolderImpl final : public CallableHolder<ReturnType, Args...> {
    Callable callable;
public:
    explicit CallableHolderImpl(Callable& c) : callable{c} {}
    explicit CallableHolderImpl(Callable&& c) : callable{std::move(c)} {}

    /**
     * To support copying, we clone the resource held by the current object
     * @return Returns a unique pointer to the cloned object
     */
    std::unique_ptr<CallableHolder<ReturnType, Args...>> clone() override {
        return std::make_unique<CallableHolderImpl>(*this);
    }

    /**
     * @param args invokes the callable with appropriate arguments using perfect forwarding
     * @return returns a value corresponding to the return type of the function
     */
    ReturnType operator()(Args&&... args) override {
        return callable(std::forward<Args>(args)...);
    }
};

/**
 * Primary template declaration needed to allow specialization of Function
 * for callable types in the form Function<ReturnType(Args...)>
 *
 * Without this, the compiler will not accept the specialization below.
 */
template<typename>
class Function;

/**
 * Template specialization of the above template. Allows a function to be passed in as ReturnType(ArgumentType...)
 * @tparam ReturnType return type of the function
 * @tparam Args parameter pack of arguments
 */
template<typename ReturnType, typename... Args>
class Function<ReturnType(Args...)> {
    std::unique_ptr<CallableHolder<ReturnType, Args...>> ptr;
public:
    template<typename Callable>
    Function(Callable fn) : ptr{new CallableHolderImpl<decltype(fn), ReturnType, Args...>(fn)} {}

    Function(const Function& fn) : ptr{fn.ptr->clone()} {}
    Function& operator=(const Function& fn) {
        if (&fn == this) {
            return *this;
        }
        ptr = fn.ptr->clone();
        return *this;
    }

    Function(Function&& fn) noexcept  = default;
    Function& operator=(Function&& fn) = default;

    ReturnType operator()(Args&&... args) {
        if (!ptr) {
            throw std::bad_function_call();
        }
        return (*ptr)(std::forward<Args>(args)...);
    }
};

int some_fn(int x, int y, int z) {
    return x + y + z;
}

int main() {
    int y = 30;
    Function<int(int, int, int)> fn1{&some_fn};
    Function<int(int)>  fn2 = [](int x) {
        return 42 + x;
    };
    Function<void(int)> fn3 = [&y](int z) {
        y += z;
    };
    std::cout << fn1(42,100,100) + 30 <<" ";

    std::cout << fn2(20) <<" ";

    fn3(50);
    std::cout << y;

    return 0;
}
