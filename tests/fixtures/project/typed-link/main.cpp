#include <dlfcn.h>
#include <pthread.h>

#if ! defined(_REENTRANT)
#error thread requirement did not reach the frontend
#endif

extern "C" double cos(double);

int main() {
    auto* handle = dlopen(nullptr, RTLD_LAZY);
    if (handle != nullptr) dlclose(handle);
    volatile double input = 0.0;
    auto            value = cos(input);
    return pthread_equal(pthread_self(), pthread_self()) && value == 1.0 ? 0 : 1;
}
