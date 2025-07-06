#include <iostream>

class String {
    size_t m_size{};
    size_t m_capacity{};
    char* m_data{nullptr};

    template<typename Self>
    static decltype(auto) access(Self& self, int index) {
        if (index < 0 || index >= self.m_size) {
            throw std::runtime_error("Invalid index access");
        }
        return self[index];
    }

    [[nodiscard]] size_t get_capacity() const {
        return std::max(2*m_capacity, static_cast<size_t>(1));
    }

    template<typename T>
    void initialize_string(const T& str, size_t n) {
        delete[] m_data;
        m_data = new char[n];
        for (size_t i = 0; i<n; i++) {
            m_data[i] = str[i];
        }
        m_size = m_capacity = n;
    }

public:
     String() = default;

     char& operator[](const size_t index) {
         return *(m_data + index);
     }

    const char& operator[](const size_t index) const {
        return *(m_data+index);
    }

     String(const char* str) {
        const size_t n = strlen(str);
        initialize_string(str, n);
    }

    String(const char& ch) {
        m_data = new char[1];
        m_data[0] = ch;
        m_size = m_capacity = 1;
    }

    String(const String& str) {
        const size_t n = str.m_size;
        initialize_string(str, n);
    }
    String& operator=(const String& str) {
        const size_t n = str.m_size;
        if (this == &str) {
            return *this;
        }
        initialize_string(str, n);
        return *this;
    }


    String& operator=(const char* str) {
         const size_t n = strlen(str);
         initialize_string(str, n);
         return *this;
     }

    String(String&& str) noexcept:
    m_size{std::exchange(str.m_size, 0)},
    m_capacity{std::exchange(str.m_capacity, 0)},
    m_data{std::exchange(str.m_data, nullptr)} {}

    String& operator=(String&& str) noexcept {
        if (this == &str) {
            return *this;
        }
        delete[] m_data;
        m_size = std::exchange(str.m_size, 0);
        m_capacity = std::exchange(str.m_capacity, 0);
        m_data = std::exchange(str.m_data, nullptr);
        return *this;
    }


    bool operator==(const String& str) const {
        if (m_size != str.size()) {
            return false;
        }
        for (int i = 0; i<str.size(); i++) {
            if (m_data[i] != str[i]) {
                return false;
            }
        }
        return true;
    }

    bool operator==(const char* str) const {
        const size_t n = strlen(str);
        if (m_size != n) {
            return false;
        }
        for (int i = 0; i<n; i++) {
            if (m_data[i] != str[i]) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] size_t size() const {
        return m_size;
    }
    [[nodiscard]] size_t capacity() const {
        return m_capacity;
    }

    void resize(const size_t size, const char ch) {
        if (size > m_size) {
            this->reserve(size);
            for (size_t i = m_size; i<size; i++) {
                m_data[i] = ch;
            }
        }
        m_size = size;
    }
    void reserve(const size_t size) {
        if (size <= m_size) {
            return ;
        }
        auto* new_m_data = new char[size];
        for (int i = 0; i<m_size; i++) {
            new_m_data[i] = m_data[i];
        }
        delete[] m_data;
        m_capacity = size;
        m_data = new_m_data;
    }

    void push_back(const char ch) {
        if (m_size == m_capacity) {
            this->reserve(get_capacity());
        }
        m_data[m_size++] = ch;
    }
    void pop_back() {
        if (m_size == 0) {
            return ;
        }
        --m_size;
    }

    [[nodiscard]] char* begin() const {
        return m_data;
    }
    [[nodiscard]] char* end() const {
        return m_data + m_size;
    }

    [[nodiscard]] char& front() {
        return access(*this, 0);
    }
    [[nodiscard]] const char& front() const {
        return access(*this, 0);
    }

    [[nodiscard]] char& back() {
        return access(*this, m_size - 1);
    }
    [[nodiscard]] const char& back() const {
        return access(*this, m_size - 1);
    }

    String operator+(const String& str) const {
        String result{*this};
        for (const char i : str) {
            result.push_back(i);
        }
        return result;
    }
    String operator+(const char* str) const {
        String result{*this};
        const size_t n = strlen(str);
         for (size_t i = 0; i<n; i++) {
             result.push_back(str[i]);
         }
         return result;
    }
    String operator+(const char ch) const {
         String result{*this};
         result.push_back(ch);
         return result;
    }

    void operator+=(const String& str) {
        *this = *this + str;
    }
    void operator+=(const char* str) {
        *this = *this + str;
    }
    void operator+=(const char ch) {
        *this = *this + ch;
    }

    friend std::ostream& operator<<(std::ostream& out, const String& str) {
        for (const char i : str) {
            out << i;
        }
        return out;
    }
    friend std::istream& operator>>(std::istream& in, String& str) {
         str = "";
         char ch;
         while (in.get(ch) && std::isspace(ch)) {}
         if (!in) return in;
         do {
             str.push_back(ch);
         } while (in.get(ch) && !std::isspace(ch));
         return in;
     }

    ~String() {
        delete[] m_data;
    }
};

int main() {
    std::cout << "=== Constructors and Operator= ===\n";
    const String s1 = "hello";
    const String& s2 = s1; // Copy constructor
    const String& s3 = s2;        // Copy assignment
    const String s4 = 'x';
    const String s5 = "world";   // const char* assignment
    String s6 = std::move(s1 + "!");  // Move constructor from temporary

    std::cout << "\n=== Operator== ===\n";
    std::cout << (s2 == "hello") << "\n";   // Should print 1
    std::cout << (s4 == "x") << "\n";       // Should print 1
    std::cout << (s2 == s3) << "\n";        // Should print 1
    std::cout << (s2 == s4) << "\n";        // Should print 0

    std::cout << "\n=== Operator+ and += ===\n";
    String joined = s2 + " " + s5;
    std::cout << joined << "\n";  // "hello world"

    joined += "!";
    std::cout << joined << "\n";  // "hello world!"

    joined += '!';
    std::cout << joined << "\n";  // "hello world!!"

    joined += s4;
    std::cout << joined << "\n";  // "hello world!!x"

    std::cout << "\n=== Indexing and Mutation ===\n";
    joined[0] = 'H';
    std::cout << joined << "\n";  // "Hello world!!x"

    std::cout << "\n=== front() and back() ===\n";
    std::cout << joined.front() << " " << joined.back() << "\n";

    std::cout << "\n=== push_back / pop_back ===\n";
    joined.push_back('?');
    std::cout << joined << "\n";

    joined.pop_back();
    std::cout << joined << "\n";

    std::cout << "\n=== resize() ===\n";
    joined.resize(20, '_');  // Pads with '_'
    std::cout << joined << " size: " << joined.size() << "\n";

    joined.resize(5, '_');   // Truncates
    std::cout << joined << " size: " << joined.size() << "\n";

    std::cout << "\n=== reserve() ===\n";
    std::cout << "Before: " << joined.capacity() << "\n";
    joined.reserve(50);
    std::cout << "After : " << joined.capacity() << "\n";

    std::cout << "\n=== begin()/end() iteration ===\n";
    for (char & it : joined) {
        std::cout << "[" << it << "]";
    }
    std::cout << "\n";

    std::cout << "\n=== Move Assignment ===\n";
    const String moved = std::move(joined);
    std::cout << "Moved: " << moved << "\n";
    return 0;
}