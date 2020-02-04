COPT = -Os

PROG = micropython-progmem-test

CFLAGS_EXTRA += -DMICROPY_UNIX_PROGMEM_TEST -I../../../capstone/include
LDFLAGS_EXTRA += -L../../../capstone/ -lcapstone
