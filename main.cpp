#include<iostream>

template<typename T>
class Vector {
public:
    Vector() = default;
    Vector(const size_t size, T value) {
        m_data = new T[size];
        m_size = m_capacity = size;
        for (size_t i = 0; i<m_size; i++) {
            m_data[i] = value;
        }
    }

    explicit Vector(size_t size) : Vector(size, T{}) {}

    Vector(std::initializer_list<T> list) {
        m_data = new T[list.size()];
        m_capacity = list.size();
        for (auto it = list.begin(); it != list.end(); ++it) {
            m_data[m_size] = *it;
            m_size++;
        }
    }

    Vector(const Vector& v) {
        m_size = m_capacity = v.size();
        m_data = new T[v.size()];
        for (size_t i = 0; i<v.size(); i++) {
            m_data[i] = v[i];
        }
    }

    Vector& operator=(const Vector& v) {

        //Handle self assignment
        if (this == &v) {
            return *this;
        }

        delete[] m_data;

        m_size = m_capacity = v.size();
        m_data = new T[v.size()];
        for (size_t i = 0; i<v.size(); i++) {
            m_data[i] = v[i];
        }
        return *this;
    }

    Vector(Vector&& v) noexcept :
    m_size{std::exchange(v.m_size, 0)},
    m_capacity{std::exchange(v.m_capacity, 0)},
    m_data{std::exchange(v.m_data, nullptr)}
    {}

    Vector& operator=(Vector&& v) noexcept {
        if (this == &v) {
            return *this;
        }

        delete[] m_data;

        m_data = std::exchange(v.m_data, nullptr);
        m_capacity = std::exchange(v.m_capacity, 0);
        m_size = std::exchange(v.m_size, 0);

        return *this;
    }

    T& operator[](int i) const {
        if (i >= m_size) {
            throw std::out_of_range("Out of bounds access");
        }
        return *(m_data + i);
    }

    [[nodiscard]] size_t size() const {
        return m_size;
    }

    [[nodiscard]] size_t capacity() const {
        return m_capacity;
    }

    T* begin() {
        return m_data;
    }

    T* end() {
        return m_data + m_size;
    }

    void push_back(const T& value) {
        if (m_size == m_capacity) {
            this->reserve(std::max(2*m_capacity, static_cast<size_t>(1)));
        }
        m_data[m_size] = value;
        m_size++;
    }

    void pop_back() {
        if (m_size == 0) {
            return ;
        }
        --m_size;
    }

    void resize(const size_t size) {
        if (size > m_size) {
            this->reserve(size);
        }
        m_size = size;
    }

    void reserve(const size_t size) {

        if (size <= m_capacity) {
            return ;
        }

        T* new_m_data = new T[size]{};

        for (size_t i = 0; i<m_size; i++) {
            new_m_data[i] = std::move(m_data[i]);
        }

        delete[] m_data;

        m_data = new_m_data;
        m_capacity = size;
    }

    void clear() {
        this->resize(0);
    }

    [[nodiscard]] bool empty() const {
        return m_size == 0;
    }

    T& back() {
        return *(m_data + m_size - 1);
    }

    void shrink_to_fit() {
        T* new_m_data = new T[m_size]{};
        for (size_t i = 0; i<m_size; i++) {
            new_m_data[i] = std::move(m_data[i]);
        }
        delete[] m_data;
        m_data = new_m_data;
        m_capacity = m_size;
    }

    ~Vector() {
        delete[] m_data;
    }

private:
    size_t m_size{};
    size_t m_capacity{};
    T* m_data{nullptr};
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