COPT = -Os

PROG = micropython-progmem-test

CFLAGS_EXTRA += -DMICROPY_UNIX_PROGMEM_TEST
LDFLAGS_EXTRA += -r -emain -Wl,--emit-relocs

MICROPY_PY_FFI = 0
