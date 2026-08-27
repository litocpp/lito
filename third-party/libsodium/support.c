#include <sodium/core.h>
#include <sodium/utils.h>

#include <stdlib.h>

void sodium_memzero(void* const pointer, const size_t length) {
    volatile unsigned char* volatile bytes = (volatile unsigned char* volatile)pointer;
    size_t index                           = 0;

    while (index < length) {
        bytes[index++] = 0;
    }
}

int sodium_is_zero(const unsigned char* bytes, const size_t length) {
    volatile unsigned char difference = 0;

    for (size_t index = 0; index < length; ++index) {
        difference |= bytes[index];
    }
    return 1 & ((difference - 1) >> 8);
}

int sodium_memcmp(const void* const left_pointer,
                  const void* const right_pointer,
                  const size_t      length) {
    const volatile unsigned char* volatile left =
        (const volatile unsigned char* volatile)left_pointer;
    const volatile unsigned char* volatile right =
        (const volatile unsigned char* volatile)right_pointer;
    volatile unsigned char difference = 0;

    for (size_t index = 0; index < length; ++index) {
        difference |= left[index] ^ right[index];
    }
    return (1 & ((difference - 1) >> 8)) - 1;
}

void sodium_misuse(void) {
    abort();
}
