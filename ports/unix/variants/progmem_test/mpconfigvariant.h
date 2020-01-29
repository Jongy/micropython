/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2020 Yonatan Goldschmidt
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdint.h>


// https://stackoverflow.com/a/3454066
#define MP_PROGMEM __attribute__((section(".progmem,\"ax\",@progbits #")))

#define PROGMEM_OFFSET 0x1000000

static inline const void *translate_progmem_address(const void *addr) {
    return (const void*)((uintptr_t)addr + PROGMEM_OFFSET);
}

static inline uint8_t load_pgmem_u8(const void *addr) {
    return *(const uint8_t*)translate_progmem_address(addr);
}

static inline uint16_t load_pgmem_u16(const void *addr) {
    return *(const uint16_t*)translate_progmem_address(addr);
}

static inline uint32_t load_pgmem_u32(const void *addr) {
    return *(const uint32_t*)translate_progmem_address(addr);
}

static inline int8_t load_pgmem_s8(const void *addr) {
    return *(const int8_t*)translate_progmem_address(addr);
}

static inline int16_t load_pgmem_s16(const void *addr) {
    return *(const int16_t*)translate_progmem_address(addr);
}

static inline int32_t load_pgmem_s32(const void *addr) {
    return *(const int32_t*)translate_progmem_address(addr);
}

#define MP_PGM_ACCESS(x)                                                       \
    __builtin_types_compatible_p(typeof(x), uint8_t)  ? load_pgmem_u8(&(x))  : \
    __builtin_types_compatible_p(typeof(x), uint16_t) ? load_pgmem_u16(&(x)) : \
    __builtin_types_compatible_p(typeof(x), uint32_t) ? load_pgmem_u32(&(x)) : \
    __builtin_types_compatible_p(typeof(x), int8_t)   ? load_pgmem_s8(&(x))  : \
    __builtin_types_compatible_p(typeof(x), int16_t)  ? load_pgmem_s16(&(x)) : \
    __builtin_types_compatible_p(typeof(x), int32_t)  ? load_pgmem_s32(&(x)) : \
    0  // just crash immediately...
