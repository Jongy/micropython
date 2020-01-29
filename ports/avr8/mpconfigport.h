#include <stdint.h>
#include <avr/pgmspace.h>

// options to control how MicroPython is built

// options based on pic16
#define MICROPY_OBJ_REPR            (MICROPY_OBJ_REPR_B)
#define MICROPY_ALLOC_PATH_MAX      (64)
#define MICROPY_EMIT_X64            (0)
#define MICROPY_EMIT_THUMB          (0)
#define MICROPY_EMIT_INLINE_THUMB   (0)
#define MICROPY_COMP_MODULE_CONST   (0)
#define MICROPY_COMP_CONST          (0)
#define MICROPY_MEM_STATS           (0)
#define MICROPY_DEBUG_PRINTERS      (0)
#define MICROPY_ENABLE_GC           (1)
#define MICROPY_REPL_EVENT_DRIVEN   (0)
#define MICROPY_HELPER_REPL         (1)
#define MICROPY_HELPER_LEXER_UNIX   (0)
#define MICROPY_ENABLE_SOURCE_LINE  (0)
#define MICROPY_ENABLE_DOC_STRING   (0)
#define MICROPY_ERROR_REPORTING     (MICROPY_ERROR_REPORTING_TERSE)
#define MICROPY_PY_ASYNC_AWAIT      (0)
#define MICROPY_PY_BUILTINS_BYTEARRAY (0)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (0)
#define MICROPY_PY_BUILTINS_FROZENSET (0)
#define MICROPY_PY_BUILTINS_SET     (0)
#define MICROPY_PY_BUILTINS_SLICE   (0)
#define MICROPY_PY_BUILTINS_PROPERTY (0)
#define MICROPY_PY_MICROPYTHON_MEM_INFO (0)
#define MICROPY_PY___FILE__         (0)
#define MICROPY_PY_GC               (0)
#define MICROPY_PY_ARRAY            (0)
#define MICROPY_PY_COLLECTIONS      (0)
#define MICROPY_PY_MATH             (0)
#define MICROPY_PY_CMATH            (0)
#define MICROPY_PY_IO               (0)
#define MICROPY_PY_STRUCT           (0)
#define MICROPY_PY_SYS              (0)
#define MICROPY_CPYTHON_COMPAT      (0)
#define MICROPY_LONGINT_IMPL        (MICROPY_LONGINT_IMPL_NONE)
#define MICROPY_FLOAT_IMPL          (MICROPY_FLOAT_IMPL_NONE)
#define MICROPY_NO_ALLOCA           (0)

// type definitions for the specific machine

#define MICROPY_MAKE_POINTER_CALLABLE(p) ((void*)((mp_uint_t)(p)))

// This port is intended to be 32-bit, but unfortunately, int32_t for
// different targets may be defined in different ways - either as int
// or as long. This requires different printf formatting specifiers
// to print such value. So, we avoid int32_t and use int directly.
#define UINT_FMT "%u"
#define INT_FMT "%d"
typedef int mp_int_t; // must be pointer size
typedef unsigned int mp_uint_t; // must be pointer size
typedef int mp_off_t;

#define MICROPY_PORT_ROOT_POINTERS \
    char *readline_hist[16];

// TODO move to unistd.h
#define ssize_t size_t
#define SEEK_SET 0
#define SEEK_CUR 1

#define MP_PLAT_PRINT_STRN(str, len) mp_hal_stdout_tx_strn_cooked(str, len)

// We need to provide a declaration/definition of alloca()
#include <alloca.h>

#ifdef __AVR_ATmega2560__
#define MICROPY_HW_BOARD_NAME "atmega2560"
#else
#error unsupported!
#endif

#define MICROPY_HW_MCU_NAME "avr"

#define MP_STATE_PORT MP_STATE_VM


#define MP_PROGMEM PROGMEM

static inline uint8_t load_pgmem_u8(const void *addr) {
    return pgm_read_byte(addr);
}

static inline uint16_t load_pgmem_u16(const void *addr) {
    return pgm_read_word(addr);
}

static inline uint32_t load_pgmem_u32(const void *addr) {
    return pgm_read_dword(addr);
}

static inline int8_t load_pgmem_s8(const void *addr) {
    return (int8_t)pgm_read_byte(addr);
}

static inline int16_t load_pgmem_s16(const void *addr) {
    return (int16_t)pgm_read_word(addr);
}

static inline int32_t load_pgmem_s32(const void *addr) {
    return (int32_t)pgm_read_dword(addr);
}

#define MP_PGM_ACCESS(x)                                                       \
    __builtin_types_compatible_p(typeof(x), uint8_t)  ? load_pgmem_u8(&(x))  : \
    __builtin_types_compatible_p(typeof(x), uint16_t) ? load_pgmem_u16(&(x)) : \
    __builtin_types_compatible_p(typeof(x), uint32_t) ? load_pgmem_u32(&(x)) : \
    __builtin_types_compatible_p(typeof(x), int8_t)   ? load_pgmem_s8(&(x))  : \
    __builtin_types_compatible_p(typeof(x), int16_t)  ? load_pgmem_s16(&(x)) : \
    __builtin_types_compatible_p(typeof(x), int32_t)  ? load_pgmem_s32(&(x)) : \
    0  // just crash immediately...
