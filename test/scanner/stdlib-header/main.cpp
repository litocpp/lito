#include <vector>

auto main() -> int {
    auto values = std::vector<int> { 1, 2, 3 };
    return values.size() == 3 ? 0 : 1;
}
