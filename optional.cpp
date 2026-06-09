#include <cassert>
#include <concepts>
#include <iostream>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

struct nullopt_t {
  explicit constexpr nullopt_t(int) {}
};
inline constexpr nullopt_t nullopt{0};

template <typename T> class Optional {
  bool valid{false};
  union Storage {
    T data;
    Storage() {}
    ~Storage() {}
  } storage;

public:
  constexpr Optional() noexcept : valid{false} {}

  Optional(nullopt_t) noexcept : valid{false} {}
  Optional &operator=(nullopt_t) noexcept {
    reset();
    return *this;
  }

  template <typename U = T>
    requires(!std::is_same_v<std::remove_cvref_t<U>, Optional> &&
             !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t> &&
             std::is_constructible_v<T, U>)
  explicit(!std::is_convertible_v<U &&, T>) Optional(U &&v) : valid{true} {
    std::construct_at(&storage.data, std::forward<U>(v));
  }

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  explicit Optional(std::in_place_t, Args &&...args) : valid{true} {
    std::construct_at(&storage.data, std::forward<Args>(args)...);
  }

  Optional(const Optional &optional) : valid{optional.valid} {
    if (optional.valid) {
      std::construct_at(&storage.data, optional.storage.data);
    }
  }
  Optional(Optional &&optional) noexcept(
      std::is_nothrow_move_constructible_v<T>)
      : valid{optional.valid} {
    if (valid) {
      std::construct_at(&storage.data, std::move(optional.storage.data));
    }
  }

  Optional &operator=(const Optional &optional) {
    if (this == &optional) {
      return *this;
    }

    if (valid && optional.valid) {
      storage.data = optional.storage.data;
    } else if (valid && !optional.valid) {
      std::destroy_at(&storage.data);
      valid = optional.valid;
    } else if (!valid && optional.valid) {
      std::construct_at(&storage.data, optional.storage.data);
      valid = optional.valid;
    }

    return *this;
  }

  Optional &operator=(Optional &&optional) noexcept(
      std::is_nothrow_move_constructible_v<T> &&
      std::is_nothrow_move_assignable_v<T>) {
    if (this == &optional) {
      return *this;
    }

    if (valid && optional.valid) {
      storage.data = std::move(optional.storage.data);
    } else if (valid && !optional.valid) {
      std::destroy_at(&storage.data);
      valid = optional.valid;
    } else if (!valid && optional.valid) {
      std::construct_at(&storage.data, std::move(optional.storage.data));
      valid = optional.valid;
    }

    return *this;
  }

  [[nodiscard]] constexpr bool has_value() const noexcept { return valid; }

  [[nodiscard]] explicit constexpr operator bool() const noexcept {
    return valid;
  }

  [[nodiscard]] T &value() & {
    if (!valid) {
      throw std::bad_optional_access();
    }
    return storage.data;
  }

  [[nodiscard]] const T &value() const & {
    if (!valid) {
      throw std::bad_optional_access();
    }
    return storage.data;
  }

  [[nodiscard]] T &&value() && {
    if (!valid) {
      throw std::bad_optional_access();
    }
    return std::move(storage.data);
  }

  [[nodiscard]] const T &&value() const && {
    if (!valid) {
      throw std::bad_optional_access();
    }
    return std::move(storage.data);
  }

  [[nodiscard]] T &operator*() & noexcept { return storage.data; }
  [[nodiscard]] const T &operator*() const & noexcept { return storage.data; }
  [[nodiscard]] T &&operator*() && noexcept { return std::move(storage.data); }
  [[nodiscard]] const T &&operator*() const && noexcept {
    return std::move(storage.data);
  }
  [[nodiscard]] T *operator->() noexcept { return &storage.data; }
  [[nodiscard]] const T *operator->() const noexcept { return &storage.data; }

  template <typename... Args>
    requires std::constructible_from<T, Args...>
  T &emplace(Args &&...args) {
    if (valid) {
      std::destroy_at(&storage.data);
    }
    valid = false;
    std::construct_at(&storage.data, std::forward<Args>(args)...);
    valid = true;
    return storage.data;
  }

  template <typename U>
  [[nodiscard]] constexpr T value_or(U &&fallback) const & {
    return has_value() ? storage.data
                       : static_cast<T>(std::forward<U>(fallback));
  }

  template <typename U> [[nodiscard]] constexpr T value_or(U &&fallback) && {
    return has_value() ? std::move(storage.data)
                       : static_cast<T>(std::forward<U>(fallback));
  }

  void reset() noexcept {
    if (valid) {
      std::destroy_at(&storage.data);
      valid = false;
    }
  }

  void swap(Optional &other) noexcept(std::is_nothrow_move_constructible_v<T> &&
                                      std::is_nothrow_swappable_v<T>) {
    if (valid && other.valid) {
      using std::swap;
      swap(storage.data, other.storage.data);
    } else if (valid && !other.valid) {
      std::construct_at(&other.storage.data, std::move(storage.data));
      std::destroy_at(&storage.data);
      other.valid = true;
      valid = false;
    } else if (!valid && other.valid) {
      other.swap(*this);
    }
  }

  friend void swap(Optional &a, Optional &b) noexcept(noexcept(a.swap(b))) {
    a.swap(b);
  }

  friend bool operator==(const Optional &a, const Optional &b) {
    if (a.valid != b.valid)
      return false;
    return !a.valid || a.storage.data == b.storage.data;
  }
  friend bool operator==(const Optional &a, nullopt_t) noexcept {
    return !a.valid;
  }

  ~Optional() {
    if (valid)
      std::destroy_at(&storage.data);
  }
};

template <typename T> Optional(T) -> Optional<T>;

struct Tracker {
  static int constructions;
  static int destructions;
  static int copies;
  static int moves;

  int val;

  explicit Tracker(const int v = 0) : val(v) { ++constructions; }
  Tracker(const Tracker &other) : val(other.val) { ++copies; }
  Tracker(Tracker &&other) noexcept : val(other.val) { ++moves; }
  ~Tracker() { ++destructions; }

  Tracker &operator=(const Tracker &other) {
    val = other.val;
    ++copies;
    return *this;
  }

  Tracker &operator=(Tracker &&other) noexcept {
    val = other.val;
    ++moves;
    return *this;
  }

  friend std::ostream &operator<<(std::ostream &os, const Tracker &t) {
    return os << "Tracker(" << t.val << ")";
  }

  static void reset() { constructions = destructions = copies = moves = 0; }
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
  Optional<Tracker> b = a;            // copy
  Optional<Tracker> c = std::move(a); // move

  assert(Tracker::constructions == 1);
  assert(Tracker::copies == 1);
  assert(Tracker::moves == 2);
}

void test_assignment() {
  Tracker::reset();
  Optional a{Tracker(1)};
  Optional<Tracker> b;
  b = a; // copy assignment
  Optional<Tracker> c;
  c = std::move(a); // move assignment

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
