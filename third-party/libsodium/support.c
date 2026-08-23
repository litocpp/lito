#include <sodium/core.h>
#include <sodium/crypto_hash_sha512.h>
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

void _crypto_sign_ed25519_ref10_hinit(crypto_hash_sha512_state* state, int prehashed) {
    static const unsigned char domain[] = {
        'S', 'i', 'g', 'E', 'd', '2', '5', '5', '1', '9', ' ', 'n', 'o', ' ', 'E', 'd', '2',
        '5', '5', '1', '9', ' ', 'c', 'o', 'l', 'l', 'i', 's', 'i', 'o', 'n', 's', 1,   0,
    };

    crypto_hash_sha512_init(state);
    if (prehashed != 0) {
        crypto_hash_sha512_update(state, domain, sizeof domain);
    }
}

void sodium_misuse(void) {
    abort();
}
