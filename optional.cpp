#include <assert.h>
#include <iostream>

template<typename T>
class Optional {
    bool valid;
    union Storage {
        char ch;
        T data;
        Storage(): ch{} {}
        ~Storage() {}
    } storage;

public:
    Optional(): valid{false} {}

    explicit Optional(T& value): valid{true} {
        new (&storage.data) T(value);
    }
    Optional(T&& value): valid{true} {
        new (&storage.data) T(std::move(value));
    }

    Optional(const Optional& optional) : valid{optional.valid} {
        new (&storage.data) T(optional.storage.data);
    }
    Optional(Optional&& optional) noexcept {
        valid = optional.valid;
        new (&storage.data) T(std::move(optional.storage.data));
        optional.valid = false;
    }

    Optional& operator=(const Optional& optional) {
        if (this == &optional) {
            return *this;
        }

        if (valid && optional.valid) {
            storage.data = optional.storage.data;
        } else if (valid && !optional.valid) {
            storage.data.~T();
            valid = optional.valid;
        } else if (!valid && optional.valid) {
            new (&storage.data) T(optional.storage.data);
            valid = optional.valid;
        }

        return *this;
    }

    Optional& operator=(Optional&& optional)  noexcept {
        if (this == &optional) {
            return *this;
        }

        if (valid && optional.valid) {
            storage.data = std::move(optional.storage.data);
        } else if (valid && !optional.valid) {
            storage.data.~T();
            valid = optional.valid;
        } else if (!valid && optional.valid) {
            new (&storage.data) T(std::move(optional.storage.data));
            valid = optional.valid;
        }

        optional.valid = false;
        return *this;
    }

    [[nodiscard]] bool has_value() const {
        return valid;
    }
    explicit operator bool() const {
        return valid;
    }
    T& value() {
        return storage.data;
    }

    T& operator*() {
        return storage.data;
    }

    template<typename... Args>
    T& emplace(Args&&... args) {
        if (valid) {
            storage.data.~T();
        }
        new (&storage.data) T(std::forward<Args>(args)...);
        valid = true;
        return storage.data;
    }

    ~Optional() {
        if (valid)
            storage.data.~T();
    }
};

struct Tracker {
    static int constructions;
    static int destructions;
    static int copies;
    static int moves;

    int val;

    explicit Tracker(const int v = 0) : val(v) { ++constructions; }
    Tracker(const Tracker& other) : val(other.val) { ++copies; }
    Tracker(Tracker&& other) noexcept : val(other.val) { ++moves; }
    ~Tracker() { ++destructions; }

    Tracker& operator=(const Tracker& other) {
        val = other.val;
        ++copies;
        return *this;
    }

    Tracker& operator=(Tracker&& other) noexcept {
        val = other.val;
        ++moves;
        return *this;
    }

    friend std::ostream& operator<<(std::ostream& os, const Tracker& t) {
        return os << "Tracker(" << t.val << ")";
    }

    static void reset() {
        constructions = destructions = copies = moves = 0;
    }
};

int Tracker::constructions = 0;
int Tracker::destructions = 0;
int Tracker::copies = 0;
int Tracker::moves = 0;


void test_basic_construction() {
    Optional<int> o;
    assert(!o.has_value());
    o = 10;
    assert(o.has_value());
    assert(o.value() == 10);
}

void test_copy_move() {
    Tracker::reset();
    Optional a{Tracker(5)};
    Optional<Tracker> b = a;  // copy
    Optional<Tracker> c = std::move(a);  // move

    assert(Tracker::constructions == 1);
    assert(Tracker::copies == 1);
    assert(Tracker::moves == 2);
}

void test_assignment() {
    Tracker::reset();
    Optional a{Tracker(1)};
    Optional<Tracker> b;
    b = a;  // copy assignment
    Optional<Tracker> c;
    c = std::move(a);  // move assignment

    assert(Tracker::copies == 1);
    assert(Tracker::moves == 2);
}

void test_emplace() {
    Tracker::reset();
    Optional<Tracker> o;
    o.emplace(100);
    assert(o.has_value());
    assert(o.value().val == 100);
    o.emplace(200);
    assert(o.value().val == 200);

    assert(Tracker::constructions == 2);  
    assert(Tracker::destructions == 1);   
}

void test_lifetime_destruction() {
    Tracker::reset();
    {
        Optional o{Tracker(5)};
        assert(o.has_value());
    }
    assert(Tracker::destructions == 2);
}


int main() {
    test_basic_construction();
    test_copy_move();
    test_assignment();
    test_emplace();
    test_lifetime_destruction();
    return 0;
}
