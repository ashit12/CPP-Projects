#include <iostream>

template<typename T>
class UniquePtr {
    T* m_ptr;
public:
    UniquePtr() : m_ptr{nullptr} {}
    explicit UniquePtr(T* ptr) : m_ptr{ptr} {}

    UniquePtr(const UniquePtr&) = delete;
    UniquePtr operator=(const UniquePtr&) = delete;

    UniquePtr(UniquePtr&& ptr) noexcept {
        m_ptr = ptr.m_ptr;
        ptr.m_ptr = nullptr;
    }
    UniquePtr& operator=(UniquePtr&& ptr) noexcept {
        if (&ptr == this) {
            return *this;
        }
        delete m_ptr;
        m_ptr = ptr.m_ptr;
        ptr.m_ptr = nullptr;
        return *this;
    }

    T& operator*() {
        return *m_ptr;
    }

    T* operator->() {
        return m_ptr;
    }

    [[nodiscard]] T* get() const {
        return m_ptr;
    }

    T* release() {
        T* temp = m_ptr;
        m_ptr = nullptr;
        return temp;
    }
    
    T* reset(T* ptr) {
        delete m_ptr;
        m_ptr = ptr;
        return m_ptr;
    }

    void swap(UniquePtr& ptr) noexcept {
        auto temp = ptr.m_ptr;
        ptr.m_ptr = m_ptr;
        m_ptr = temp;
    }

    ~UniquePtr() {
        delete m_ptr;
    }
};

struct A {
    A(int x) : x(x) { std::cout << "A(" << x << ") constructed\n"; }
    ~A() { std::cout << "A(" << x << ") destroyed\n"; }
    int x;
};

struct B {
    B() { std::cout << "B() constructed\n"; }
    ~B() { std::cout << "B() destroyed\n"; }
};

int main() {
    std::cout << "Test 1: Basic construction and destruction\n";
    {
        UniquePtr<A> uptr(new A(10));
        std::cout << "Value: " << uptr.get()->x << "\n";
    }

    std::cout << "\nTest 2: Move Constructor\n";
    {
        UniquePtr<A> up1(new A(42));
        UniquePtr<A> up2(std::move(up1));
        if (up1.get() == nullptr) std::cout << "up1 is null after move\n";
        std::cout << "up2 owns: " << up2.get()->x << "\n";
    }

    std::cout << "\nTest 3: Move Assignment\n";
    {
        UniquePtr<A> up1(new A(55));
        UniquePtr<A> up2;
        up2 = std::move(up1);
        if (up1.get() == nullptr) std::cout << "up1 is null after move\n";
        std::cout << "up2 owns: " << up2.get()->x << "\n";
    }

    std::cout << "\nTest 4: release()\n";
    {
        UniquePtr<A> up(new A(77));
        A* raw = up.release();
        std::cout << "Raw value: " << raw->x << "\n";
        std::cout << "up is now: " << (up.get() == nullptr ? "null" : "not null") << "\n";
        delete raw; // manual cleanup!
    }

    std::cout << "\nTest 5: reset() with new value\n";
    {
        UniquePtr<A> up(new A(1));
        up.reset(new A(2)); // should delete A(1)
        std::cout << "Now points to: " << up.get()->x << "\n";
    }

    std::cout << "\nTest 6: reset() to nullptr\n";
    {
        UniquePtr<A> up(new A(3));
        up.reset(nullptr); // should delete A(3)
        std::cout << "Reset to nullptr. up.get() = " << (up.get() == nullptr ? "null" : "not null") << "\n";
    }

    std::cout << "\nTest 7: swap()\n";
    {
        UniquePtr<A> a(new A(5));
        UniquePtr<A> b(new A(9));
        a.swap(b);
        std::cout << "a now has: " << a.get()->x << "\n";
        std::cout << "b now has: " << b.get()->x << "\n";
    }

    std::cout << "\nTest 8: operator* and operator->\n";
    {
        UniquePtr<A> up(new A(88));
        std::cout << "Via *: " << (*up).x << "\n";
        std::cout << "Via ->: " << up->x << "\n";
    }

    std::cout << "\nTest 9: Self move-assignment (should do nothing)\n";
    {
        UniquePtr<A> up(new A(999));
        up = std::move(up); // should not crash or delete twice
        if (up.get()) std::cout << "Still owns: " << up->x << "\n";
    }

    std::cout << "\nTest 10: Chaining release and reset\n";
    {
        UniquePtr<A> up(new A(123));
        A* raw = up.release();
        UniquePtr<A> up2;
        up2.reset(raw); // now up2 takes over
        std::cout << "up2 now owns: " << up2->x << "\n";
    }

    return 0;
}
