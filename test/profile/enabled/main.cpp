#ifndef __cpp_exceptions
#error "exceptions must be enabled by default"
#endif

#ifndef __cpp_rtti
#error "RTTI must be enabled by default"
#endif

struct Base {
    virtual ~Base() = default;
};

struct Derived : Base {};

int main() {
    Derived value;
    Base*   base = &value;
    try {
        if (dynamic_cast<Derived*>(base) == nullptr) return 1;
        throw 7;
    } catch (int result) {
        return result == 7 ? 0 : 2;
    }
}
