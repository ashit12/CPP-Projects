#include <iostream>
#include <memory>
#include <stdexcept>

template <typename T> class Vector {
public:
  Vector() = default;
  Vector(size_t size, const T &val) {
    m_size = m_capacity = size;
    m_data = static_cast<T *>(operator new[](sizeof(T) * size));
    size_t i = 0;
    try {
      for (; i < size; i++)
        std::construct_at(m_data + i, val);
    } catch (...) {
      for (size_t j = 0; j < i; j++) {
        std::destroy_at(m_data + j);
      }
      operator delete[](m_data);
      throw;
    }
  }

  explicit Vector(size_t size) : Vector(size, T{}) {}

  Vector(std::initializer_list<T> list)
      : m_size{list.size()}, m_capacity{list.size()} {
    m_data = static_cast<T *>(operator new[](sizeof(T) * m_capacity));
    size_t i = 0;
    try {
      for (const auto &val : list) {
        std::construct_at(m_data + i, val);
        ++i;
      }
    } catch (...) {
      for (size_t j = 0; j < i; ++j) {
        std::destroy_at(m_data + j);
      }
      operator delete[](m_data);
      throw;
    }
  }

  Vector(const Vector &v) : m_size{v.m_size}, m_capacity{v.m_capacity} {
    m_data = static_cast<T *>(operator new[](sizeof(T) * m_capacity));
    size_t i = 0;
    try {
      for (const auto &val : v) {
        std::construct_at(m_data + i, val);
        ++i;
      }
    } catch (...) {
      for (size_t j = 0; j < i; ++j) {
        std::destroy_at(m_data + j);
      }
      operator delete[](m_data);
      throw;
    }
  }

  Vector &operator=(const Vector &v) {
    // Handle self assignment
    if (this == &v) {
      return *this;
    }
    Vector tmp(v);
    swap(tmp);
    return *this;
  }

  Vector(Vector &&v) noexcept
      : m_size{std::exchange(v.m_size, 0)},
        m_capacity{std::exchange(v.m_capacity, 0)},
        m_data{std::exchange(v.m_data, nullptr)} {}

  Vector &operator=(Vector &&v) noexcept {
    if (this == &v) {
      return *this;
    }

    for (size_t i = 0; i < m_size; ++i) {
      std::destroy_at(m_data + i);
    }
    operator delete[](m_data);

    m_data = std::exchange(v.m_data, nullptr);
    m_capacity = std::exchange(v.m_capacity, 0);
    m_size = std::exchange(v.m_size, 0);

    return *this;
  }

  T &operator[](size_t i) {
    if (i >= m_size) {
      throw std::out_of_range("Out of bounds access");
    }
    return *(m_data + i);
  }
  const T &operator[](size_t i) const {
    if (i >= m_size) {
      throw std::out_of_range("Out of bounds access");
    }
    return *(m_data + i);
  }

  T &at(size_t i) {
    if (i >= m_size)
      throw std::out_of_range("Vector::at");
    return m_data[i];
  }
  const T &at(size_t i) const {
    if (i >= m_size)
      throw std::out_of_range("Vector::at");
    return m_data[i];
  }

  [[nodiscard]] size_t size() const { return m_size; }

  [[nodiscard]] size_t capacity() const { return m_capacity; }

  T *begin() { return m_data; }
  const T *begin() const { return m_data; }

  T *end() { return m_data + m_size; }
  const T *end() const { return m_data + m_size; }

  void push_back(const T &value) {
    if (m_size == m_capacity) {
      this->reserve(std::max(2 * m_capacity, static_cast<size_t>(1)));
    }
    std::construct_at(m_data + m_size, value);
    m_size++;
  }
  void push_back(T &&value) {
    if (m_size == m_capacity) {
      this->reserve(std::max(2 * m_capacity, static_cast<size_t>(1)));
    }
    std::construct_at(m_data + m_size, std::move(value));
    m_size++;
  }

  template <typename... Args> T &emplace_back(Args &&...args) {
    if (m_size == m_capacity)
      reserve(std::max(2 * m_capacity, size_t(1)));
    std::construct_at(&m_data[m_size], std::forward<Args>(args)...);
    return m_data[m_size++];
  }

  void pop_back() noexcept {
    if (m_size == 0) {
      return;
    }
    --m_size;
    std::destroy_at(m_data + m_size);
  }

  void resize(const size_t size, const T &val = T{}) {
    if (size > m_size) {
      this->reserve(size);
      for (size_t i = m_size; i < size; ++i)
        std::construct_at(m_data + i, val);
    } else {
      while (m_size > size) {
        --m_size;
        std::destroy_at(m_data + m_size);
      }
    }
    m_size = size;
  }

  void reserve(const size_t size) {
    if (size <= m_capacity) {
      return;
    }

    T *new_m_data = static_cast<T *>(operator new[](sizeof(T) * size));
    for (size_t i = 0; i < m_size; i++) {
      std::construct_at(new_m_data + i, std::move(m_data[i]));
    }

    for (size_t j = 0; j < m_size; ++j) {
      std::destroy_at(m_data + j);
    }
    operator delete[](m_data);

    m_data = new_m_data;
    m_capacity = size;
  }

  void clear() noexcept {
    for (size_t i = 0; i < m_size; ++i) {
      std::destroy_at(m_data + i);
    }
    m_size = 0;
  }

  [[nodiscard]] bool empty() const { return m_size == 0; }

  [[nodiscard]] T &back() {
    if (m_size == 0) {
      throw std::out_of_range("cannot call back() for empty vector");
    }
    return *(m_data + m_size - 1);
  }
  [[nodiscard]] const T &back() const {
    if (m_size == 0) {
      throw std::out_of_range("cannot call back() for empty vector");
    }
    return *(m_data + m_size - 1);
  }

  [[nodiscard]] T &front() {
    if (m_size == 0) {
      throw std::out_of_range("cannot call front() for empty vector");
    }
    return *m_data;
  }
  [[nodiscard]] const T &front() const {
    if (m_size == 0) {
      throw std::out_of_range("cannot call front() for empty vector");
    }
    return *m_data;
  }

  T *data() noexcept { return m_data; }
  const T *data() const noexcept { return m_data; }

  void swap(Vector &other) noexcept {
    std::swap(m_data, other.m_data);
    std::swap(m_size, other.m_size);
    std::swap(m_capacity, other.m_capacity);
  }
  friend void swap(Vector &a, Vector &b) noexcept { a.swap(b); }

  void shrink_to_fit() {
    T *new_m_data = static_cast<T *>(operator new[](sizeof(T) * m_size));
    for (size_t i = 0; i < m_size; i++) {
      std::construct_at(new_m_data + i, std::move(m_data[i]));
    }
    for (size_t j = 0; j < m_size; ++j) {
      std::destroy_at(m_data + j);
    }
    operator delete[](m_data);
    m_data = new_m_data;
    m_capacity = m_size;
  }

  ~Vector() {
    for (size_t j = 0; j < m_size; ++j) {
      std::destroy_at(m_data + j);
    }
    operator delete[](m_data);
  }

private:
  size_t m_size{};
  size_t m_capacity{};
  T *m_data{nullptr};
};

int main() {

  Vector<int> v;
  v.resize(5);
  v.reserve(10);
  std::cout << v.size() << " " << v.capacity() << std::endl;
  v.shrink_to_fit();
  std::cout << v.size() << " " << v.capacity() << std::endl;

  return 0;
}