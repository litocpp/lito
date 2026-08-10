#include <cstdlib>
#include <cstring>

auto main() -> int {
    const auto* path = std::getenv("PATH");
    return path != nullptr && std::strstr(path, "append-path/tools") != nullptr ? 0 : 1;
}
