#include <csignal>

int main() {
    std::raise(SIGTERM);
    return 0;
}
