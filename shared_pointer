#include <iostream>
#include <utility>

template<typename T>
class SharedPtr {
    T* m_ptr;
    size_t* m_count;
    void reduce_ownership() const {
        if (m_count != nullptr) {
            --(*m_count);
            if (*m_count == 0) {
                delete m_count;
                delete m_ptr;
            }
        }

    }

public:

    SharedPtr(): m_ptr{nullptr}, m_count{nullptr} {}

    explicit SharedPtr(T* ptr) : m_ptr{ptr}, m_count{new size_t(1)} {}

    SharedPtr(const SharedPtr& ptr) {
        if (ptr.m_ptr != nullptr) {
            ++(*(ptr.m_count));
            m_ptr = ptr.m_ptr;
            m_count = ptr.m_count;
        } else {
            m_ptr = nullptr;
            m_count = nullptr;
        }
    }
    SharedPtr& operator=(const SharedPtr& ptr) {
        if (this == &ptr) {
            return *this;
        }

        reduce_ownership();

        if (ptr.m_ptr) {
            ++(*(ptr.m_count));
            m_ptr = ptr.m_ptr;
            m_count = ptr.m_count;
        } else {
            m_ptr = nullptr;
            m_count = nullptr;
        }

        return *this;
    }

    SharedPtr(SharedPtr&& ptr) noexcept : m_ptr{std::exchange(ptr.m_ptr, nullptr)}, m_count{std::exchange(ptr.m_count, nullptr)} {}
    SharedPtr& operator=(SharedPtr&& ptr) noexcept {
        if (this == &ptr) {
            return *this;
        }

        reduce_ownership();

        m_ptr = std::exchange(ptr.m_ptr, nullptr);
        m_count = std::exchange(ptr.m_count, nullptr);
        return *this;
    }

    T* get() {
        return m_ptr;
    }

    T& operator*() {
        return *m_ptr;
    }

    T* operator->() {
        return m_ptr;
    }

    [[nodiscard]] size_t use_count() const {
        return *m_count;
    }

    explicit operator bool() const {
        return m_ptr != nullptr;
    }

    void reset(T* ptr) {
        reduce_ownership();
        if (ptr != nullptr) {
            m_count = new size_t(1);
            m_ptr = ptr;
        } else {
            m_count = nullptr;
            m_ptr = nullptr;
        }
    }

    ~SharedPtr() {
        reduce_ownership();
    }
};

class Sample {
    int data;
public:
    explicit Sample(const int y) : data{y} {
        std::cout << "Constructed\n";
    }
    ~Sample() {
        std::cout << "Destructed\n";
    }
    [[nodiscard]] int get() const {
        return data;
    }
};

int main() {
    std::cout << "Test 1: Basic ownership and use_count\n";
    SharedPtr<Sample> s1{new Sample(5)};
    std::cout << "s1 use_count: " << s1.use_count() << "\n";

    std::cout << "\nTest 2: Copy constructor\n";
    SharedPtr<Sample> s2 = s1;
    std::cout << "s1 use_count: " << s1.use_count() << "\n";
    std::cout << "s2 use_count: " << s2.use_count() << "\n";

    std::cout << "\nTest 3: Copy assignment\n";
    SharedPtr<Sample> s3;
    s3 = s2;
    std::cout << "s3 use_count: " << s3.use_count() << "\n";

    std::cout << "\nTest 4: Move constructor\n";
    SharedPtr<Sample> s4 = SharedPtr<Sample>{new Sample(100)};
    std::cout << "s4 use_count: " << s4.use_count() << "\n";

    std::cout << "\nTest 5: Move assignment\n";
    SharedPtr<Sample> s5;
    s5 = SharedPtr<Sample>{new Sample(200)};
    std::cout << "s5 use_count: " << s5.use_count() << "\n";

    std::cout << "\nTest 6: Reset with new pointer\n";
    s5.reset(new Sample(300));
    std::cout << "s5 reset to new Sample. use_count: " << s5.use_count() << "\n";

    std::cout << "\nTest 7: Reset to nullptr\n";
    s5.reset(nullptr);
    std::cout << "s5 reset to nullptr. Should not crash.\n";

    std::cout << "\nTest 8: Operator overloads\n";
    std::cout << "s1 points to: " << s1->get() << ", dereferenced: " << (*s1).get() << "\n";

    std::cout << "\nTest 9: Bool operator\n";
    if (s1) {
        std::cout << "s1 is valid\n";
    }
    SharedPtr<Sample> empty;
    if (!empty) {
        std::cout << "empty is null\n";
    }

    std::cout << "\nTest 10: Destruction\n";
    {
        SharedPtr<Sample> s6 = s1;
        std::cout << "Inner scope s6 use_count: " << s6.use_count() << "\n";
    } // s6 goes out of scope
    std::cout << "After s6 destruction, s1 use_count: " << s1.use_count() << "\n";

    std::cout << "\nTest 11: Self-assignment (copy & move)\n";
    s1 = s1; // copy self-assign
    s4 = std::move(s4); // move self-assign
    std::cout << "Self-assignment passed without issues\n";

    return 0;
}
