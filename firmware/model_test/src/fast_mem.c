/* Word-at-a-time memcpy/memset, used instead of newlib-nano's byte loops.
 * Built with -fno-tree-loop-distribute-patterns: without it, at -O2/-O3 GCC
 * turns these loops back into calls to memcpy/memset, i.e. into themselves,
 * and the stack overflows (seen on the board as a HardFault). */
#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if ((((uintptr_t)d | (uintptr_t)s) & 3u) == 0) {
        uint32_t *dw = (uint32_t *)d;
        const uint32_t *sw = (const uint32_t *)s;
        while (n >= 16) { dw[0] = sw[0]; dw[1] = sw[1]; dw[2] = sw[2]; dw[3] = sw[3]; dw += 4; sw += 4; n -= 16; }
        while (n >= 4) { *dw++ = *sw++; n -= 4; }
        d = (uint8_t *)dw;
        s = (const uint8_t *)sw;
    }
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *p, int c, size_t n) {
    uint8_t *d = (uint8_t *)p;
    while (n && ((uintptr_t)d & 3u)) { *d++ = (uint8_t)c; n--; }
    uint32_t w = (uint8_t)c;
    w |= w << 8;
    w |= w << 16;
    uint32_t *dw = (uint32_t *)d;
    while (n >= 16) { dw[0] = w; dw[1] = w; dw[2] = w; dw[3] = w; dw += 4; n -= 16; }
    while (n >= 4) { *dw++ = w; n -= 4; }
    d = (uint8_t *)dw;
    while (n--) *d++ = (uint8_t)c;
    return p;
}
