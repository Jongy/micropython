COPT = -Os

PROG = micropython-progmem

CFLAGS_EXTRA += -DMICROPY_UNIX_PROGMEM_TEST -I../../../capstone/include -no-pie
LDFLAGS_EXTRA += -L../../../capstone/ -lcapstone -no-pie
