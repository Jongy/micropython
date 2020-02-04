/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 * Copyright (c) 2014-2017 Paul Sokolovsky
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

#if MICROPY_UNIX_PROGMEM_TEST
#define _GNU_SOURCE
#include <sys/mman.h>
#include <capstone/capstone.h>
#include <fcntl.h>
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/builtin.h"
#include "py/repl.h"
#include "py/gc.h"
#include "py/stackctrl.h"
#include "py/mphal.h"
#include "py/mpthread.h"
#include "extmod/misc.h"
#include "extmod/vfs.h"
#include "extmod/vfs_posix.h"
#include "genhdr/mpversion.h"
#include "input.h"

// Command line options, with their defaults
STATIC bool compile_only = false;
STATIC uint emit_opt = MP_EMIT_OPT_NONE;

#if MICROPY_ENABLE_GC
// Heap size of GC heap (if enabled)
// Make it larger on a 64 bit machine, because pointers are larger.
long heap_size = 1024*1024 * (sizeof(mp_uint_t) / 4);
#endif

STATIC void stderr_print_strn(void *env, const char *str, size_t len) {
    (void)env;
    ssize_t dummy = write(STDERR_FILENO, str, len);
    mp_uos_dupterm_tx_strn(str, len);
    (void)dummy;
}

const mp_print_t mp_stderr_print = {NULL, stderr_print_strn};

#define FORCED_EXIT (0x100)
// If exc is SystemExit, return value where FORCED_EXIT bit set,
// and lower 8 bits are SystemExit value. For all other exceptions,
// return 1.
STATIC int handle_uncaught_exception(mp_obj_base_t *exc) {
    // check for SystemExit
    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(exc->type), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
        // None is an exit value of 0; an int is its value; anything else is 1
        mp_obj_t exit_val = mp_obj_exception_get_value(MP_OBJ_FROM_PTR(exc));
        mp_int_t val = 0;
        if (exit_val != mp_const_none && !mp_obj_get_int_maybe(exit_val, &val)) {
            val = 1;
        }
        return FORCED_EXIT | (val & 255);
    }

    // Report all other exceptions
    mp_obj_print_exception(&mp_stderr_print, MP_OBJ_FROM_PTR(exc));
    return 1;
}

#define LEX_SRC_STR (1)
#define LEX_SRC_VSTR (2)
#define LEX_SRC_FILENAME (3)
#define LEX_SRC_STDIN (4)

// Returns standard error codes: 0 for success, 1 for all other errors,
// except if FORCED_EXIT bit is set then script raised SystemExit and the
// value of the exit is in the lower 8 bits of the return value
STATIC int execute_from_lexer(int source_kind, const void *source, mp_parse_input_kind_t input_kind, bool is_repl) {
    mp_hal_set_interrupt_char(CHAR_CTRL_C);

    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        // create lexer based on source kind
        mp_lexer_t *lex;
        if (source_kind == LEX_SRC_STR) {
            const char *line = source;
            lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, line, strlen(line), false);
        } else if (source_kind == LEX_SRC_VSTR) {
            const vstr_t *vstr = source;
            lex = mp_lexer_new_from_str_len(MP_QSTR__lt_stdin_gt_, vstr->buf, vstr->len, false);
        } else if (source_kind == LEX_SRC_FILENAME) {
            lex = mp_lexer_new_from_file((const char*)source);
        } else { // LEX_SRC_STDIN
            lex = mp_lexer_new_from_fd(MP_QSTR__lt_stdin_gt_, 0, false);
        }

        qstr source_name = lex->source_name;

        #if MICROPY_PY___FILE__
        if (input_kind == MP_PARSE_FILE_INPUT) {
            mp_store_global(MP_QSTR___file__, MP_OBJ_NEW_QSTR(source_name));
        }
        #endif

        mp_parse_tree_t parse_tree = mp_parse(lex, input_kind);

        #if defined(MICROPY_UNIX_COVERAGE)
        // allow to print the parse tree in the coverage build
        if (mp_verbose_flag >= 3) {
            printf("----------------\n");
            mp_parse_node_print(parse_tree.root, 0);
            printf("----------------\n");
        }
        #endif

        mp_obj_t module_fun = mp_compile(&parse_tree, source_name, is_repl);

        if (!compile_only) {
            // execute it
            mp_call_function_0(module_fun);
            // check for pending exception
            if (MP_STATE_VM(mp_pending_exception) != MP_OBJ_NULL) {
                mp_obj_t obj = MP_STATE_VM(mp_pending_exception);
                MP_STATE_VM(mp_pending_exception) = MP_OBJ_NULL;
                nlr_raise(obj);
            }
        }

        mp_hal_set_interrupt_char(-1);
        nlr_pop();
        return 0;

    } else {
        // uncaught exception
        mp_hal_set_interrupt_char(-1);
        return handle_uncaught_exception(nlr.ret_val);
    }
}

#if MICROPY_USE_READLINE == 1
#include "lib/mp-readline/readline.h"
#else
STATIC char *strjoin(const char *s1, int sep_char, const char *s2) {
    int l1 = strlen(s1);
    int l2 = strlen(s2);
    char *s = malloc(l1 + l2 + 2);
    memcpy(s, s1, l1);
    if (sep_char != 0) {
        s[l1] = sep_char;
        l1 += 1;
    }
    memcpy(s + l1, s2, l2);
    s[l1 + l2] = 0;
    return s;
}
#endif

STATIC int do_repl(void) {
    mp_hal_stdout_tx_str("MicroPython " MICROPY_GIT_TAG " on " MICROPY_BUILD_DATE "; "
        MICROPY_PY_SYS_PLATFORM " version\nUse Ctrl-D to exit, Ctrl-E for paste mode\n");

    #if MICROPY_USE_READLINE == 1

    // use MicroPython supplied readline

    vstr_t line;
    vstr_init(&line, 16);
    for (;;) {
        mp_hal_stdio_mode_raw();

    input_restart:
        vstr_reset(&line);
        int ret = readline(&line, ">>> ");
        mp_parse_input_kind_t parse_input_kind = MP_PARSE_SINGLE_INPUT;

        if (ret == CHAR_CTRL_C) {
            // cancel input
            mp_hal_stdout_tx_str("\r\n");
            goto input_restart;
        } else if (ret == CHAR_CTRL_D) {
            // EOF
            printf("\n");
            mp_hal_stdio_mode_orig();
            vstr_clear(&line);
            return 0;
        } else if (ret == CHAR_CTRL_E) {
            // paste mode
            mp_hal_stdout_tx_str("\npaste mode; Ctrl-C to cancel, Ctrl-D to finish\n=== ");
            vstr_reset(&line);
            for (;;) {
                char c = mp_hal_stdin_rx_chr();
                if (c == CHAR_CTRL_C) {
                    // cancel everything
                    mp_hal_stdout_tx_str("\n");
                    goto input_restart;
                } else if (c == CHAR_CTRL_D) {
                    // end of input
                    mp_hal_stdout_tx_str("\n");
                    break;
                } else {
                    // add char to buffer and echo
                    vstr_add_byte(&line, c);
                    if (c == '\r') {
                        mp_hal_stdout_tx_str("\n=== ");
                    } else {
                        mp_hal_stdout_tx_strn(&c, 1);
                    }
                }
            }
            parse_input_kind = MP_PARSE_FILE_INPUT;
        } else if (line.len == 0) {
            if (ret != 0) {
                printf("\n");
            }
            goto input_restart;
        } else {
            // got a line with non-zero length, see if it needs continuing
            while (mp_repl_continue_with_input(vstr_null_terminated_str(&line))) {
                vstr_add_byte(&line, '\n');
                ret = readline(&line, "... ");
                if (ret == CHAR_CTRL_C) {
                    // cancel everything
                    printf("\n");
                    goto input_restart;
                } else if (ret == CHAR_CTRL_D) {
                    // stop entering compound statement
                    break;
                }
            }
        }

        mp_hal_stdio_mode_orig();

        ret = execute_from_lexer(LEX_SRC_VSTR, &line, parse_input_kind, true);
        if (ret & FORCED_EXIT) {
            return ret;
        }
    }

    #else

    // use simple readline

    for (;;) {
        char *line = prompt(">>> ");
        if (line == NULL) {
            // EOF
            return 0;
        }
        while (mp_repl_continue_with_input(line)) {
            char *line2 = prompt("... ");
            if (line2 == NULL) {
                break;
            }
            char *line3 = strjoin(line, '\n', line2);
            free(line);
            free(line2);
            line = line3;
        }

        int ret = execute_from_lexer(LEX_SRC_STR, line, MP_PARSE_SINGLE_INPUT, true);
        if (ret & FORCED_EXIT) {
            return ret;
        }
        free(line);
    }

    #endif
}

STATIC int do_file(const char *file) {
    return execute_from_lexer(LEX_SRC_FILENAME, file, MP_PARSE_FILE_INPUT, false);
}

STATIC int do_str(const char *str) {
    return execute_from_lexer(LEX_SRC_STR, str, MP_PARSE_FILE_INPUT, false);
}

STATIC int usage(char **argv) {
    printf(
"usage: %s [<opts>] [-X <implopt>] [-c <command>] [<filename>]\n"
"Options:\n"
"-v : verbose (trace various operations); can be multiple\n"
"-O[N] : apply bytecode optimizations of level N\n"
"\n"
"Implementation specific options (-X):\n", argv[0]
);
    int impl_opts_cnt = 0;
    printf(
"  compile-only                 -- parse and compile only\n"
#if MICROPY_EMIT_NATIVE
"  emit={bytecode,native,viper} -- set the default code emitter\n"
#else
"  emit=bytecode                -- set the default code emitter\n"
#endif
);
    impl_opts_cnt++;
#if MICROPY_ENABLE_GC
    printf(
"  heapsize=<n>[w][K|M] -- set the heap size for the GC (default %ld)\n"
, heap_size);
    impl_opts_cnt++;
#endif

    if (impl_opts_cnt == 0) {
        printf("  (none)\n");
    }

    return 1;
}

// Process options which set interpreter init options
STATIC void pre_process_options(int argc, char **argv) {
    for (int a = 1; a < argc; a++) {
        if (argv[a][0] == '-') {
            if (strcmp(argv[a], "-X") == 0) {
                if (a + 1 >= argc) {
                    exit(usage(argv));
                }
                if (0) {
                } else if (strcmp(argv[a + 1], "compile-only") == 0) {
                    compile_only = true;
                } else if (strcmp(argv[a + 1], "emit=bytecode") == 0) {
                    emit_opt = MP_EMIT_OPT_BYTECODE;
                #if MICROPY_EMIT_NATIVE
                } else if (strcmp(argv[a + 1], "emit=native") == 0) {
                    emit_opt = MP_EMIT_OPT_NATIVE_PYTHON;
                } else if (strcmp(argv[a + 1], "emit=viper") == 0) {
                    emit_opt = MP_EMIT_OPT_VIPER;
                #endif
#if MICROPY_ENABLE_GC
                } else if (strncmp(argv[a + 1], "heapsize=", sizeof("heapsize=") - 1) == 0) {
                    char *end;
                    heap_size = strtol(argv[a + 1] + sizeof("heapsize=") - 1, &end, 0);
                    // Don't bring unneeded libc dependencies like tolower()
                    // If there's 'w' immediately after number, adjust it for
                    // target word size. Note that it should be *before* size
                    // suffix like K or M, to avoid confusion with kilowords,
                    // etc. the size is still in bytes, just can be adjusted
                    // for word size (taking 32bit as baseline).
                    bool word_adjust = false;
                    if ((*end | 0x20) == 'w') {
                        word_adjust = true;
                        end++;
                    }
                    if ((*end | 0x20) == 'k') {
                        heap_size *= 1024;
                    } else if ((*end | 0x20) == 'm') {
                        heap_size *= 1024 * 1024;
                    } else {
                        // Compensate for ++ below
                        --end;
                    }
                    if (*++end != 0) {
                        goto invalid_arg;
                    }
                    if (word_adjust) {
                        heap_size = heap_size * BYTES_PER_WORD / 4;
                    }
                    // If requested size too small, we'll crash anyway
                    if (heap_size < 700) {
                        goto invalid_arg;
                    }
#endif
                } else {
invalid_arg:
                    printf("Invalid option\n");
                    exit(usage(argv));
                }
                a++;
            }
        }
    }
}

STATIC void set_sys_argv(char *argv[], int argc, int start_arg) {
    for (int i = start_arg; i < argc; i++) {
        mp_obj_list_append(mp_sys_argv, MP_OBJ_NEW_QSTR(qstr_from_str(argv[i])));
    }
}

#ifdef _WIN32
#define PATHLIST_SEP_CHAR ';'
#else
#define PATHLIST_SEP_CHAR ':'
#endif

MP_NOINLINE int main_(int argc, char **argv);

#if MICROPY_UNIX_PROGMEM_TEST
extern unsigned char __start_progmem;
extern unsigned char __stop_progmem;
static unsigned long text;

static int progmem_fd;
static void progmem_printf(const char *fmt, ...) {
    char str[1024];
    va_list ap;
    va_start(ap, fmt);
    int ret = vsnprintf(str, sizeof(str), fmt, ap);
    if (ret == sizeof(str)) {
        abort();
    }
    va_end(ap);

    if (ret != write(progmem_fd, str, ret)) {
        abort();
    }
}

static unsigned long capstone_reg_to_greg(csh handle, x86_reg reg) {
    switch (reg) {
    case X86_REG_EAX:
    case X86_REG_RAX: return REG_RAX;
    case X86_REG_RBX: return REG_RBX;
    case X86_REG_RDI: return REG_RDI;
    case X86_REG_RSI: return REG_RSI;
    case X86_REG_RCX: return REG_RCX;
    case X86_REG_EDX:
    case X86_REG_RDX: return REG_RDX;
    case X86_REG_RIP: return REG_RIP;
    case X86_REG_R8:  return REG_R8;
    default: printf("invalid reg %d %s\n", reg, cs_reg_name(handle, reg)); assert(0);
    }
}

static void sigsegv_handler(int sig, siginfo_t *si, void *ctx)
{
    ucontext_t *u = (ucontext_t *const)ctx;
    const unsigned long addr = (unsigned long)si->si_addr;

    if (!((unsigned long)&__start_progmem <= addr && addr < (unsigned long)&__stop_progmem)) {
        printf("Another SIGSEGV! (address = %lx) exiting...\n", addr);
        exit(1);
    }

    unsigned long pc = (unsigned long)u->uc_mcontext.gregs[REG_RIP];

    csh handle;
    cs_insn *insn;
    size_t count;

    (void)cs_open(CS_ARCH_X86, CS_MODE_64, &handle);
    cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);

    count = cs_disasm(handle, (void*)pc, 15, pc, 0, &insn);
    assert(count >= 1);
    cs_insn *n = &insn[0];
    cs_x86 *x86 = &n->detail->x86;

#if 1
    printf("prefix[0] = %x prefix[1] = %x prefix[2] = %x prefix[3] = %x", x86->prefix[0], x86->prefix[1], x86->prefix[2], x86->prefix[3]);
    printf(" ; rex: 0x%x", x86->rex);
    printf(" ; addr_size: %u", x86->addr_size);
    printf(" ; modrm: 0x%x", x86->modrm);
    printf(" ; disp: 0x%" PRIx64 "", x86->disp);
    printf("\n\n");
    int i;
    for (i = 0; i < x86->op_count; i++) {
        cs_x86_op *op = &(x86->operands[i]);

        switch((int)op->type) {
            case X86_OP_REG:
                printf(" ; operands[%u].type: REG = %s", i, cs_reg_name(handle, op->reg));
                break;
            case X86_OP_IMM:
                printf(" ; operands[%u].type: IMM = 0x%" PRIx64 "", i, op->imm);
                break;
            case X86_OP_MEM:
                printf(" ; operands[%u].type: MEM", i);
                if (op->mem.segment != X86_REG_INVALID)
                    printf(" ; operands[%u].mem.segment: REG = %s", i, cs_reg_name(handle, op->mem.segment));
                if (op->mem.base != X86_REG_INVALID)
                    printf(" ; operands[%u].mem.base: REG = %s", i, cs_reg_name(handle, op->mem.base));
                if (op->mem.index != X86_REG_INVALID)
                    printf(" ; operands[%u].mem.index: REG = %s", i, cs_reg_name(handle, op->mem.index));
                if (op->mem.scale != 1)
                    printf(" ; operands[%u].mem.scale: %u", i, op->mem.scale);
                if (op->mem.disp != 0)
                    printf(" ; operands[%u].mem.disp: 0x%" PRIx64 "", i, op->mem.disp);
                break;
            default:
                break;
        }

        printf(" ; operands[%u].size: %u", i, op->size);

        switch(op->access) {
            default:
                break;
            case CS_AC_READ:
                printf(" ; operands[%u].access: READ", i);
                break;
            case CS_AC_WRITE:
                printf(" ; operands[%u].access: WRITE", i);
                break;
            case CS_AC_READ | CS_AC_WRITE:
                printf(" ; operands[%u].access: READ | WRITE", i);
                break;
        }
    }
    printf("\n\n");
    printf("0x%"PRIx64":\t%s\t\t%s\n", n->address, n->mnemonic, n->op_str);
#endif

    // so. operand 2 (source) should match the faulting memory address.
    assert(x86->op_count == 2);
    cs_x86_op *src = &x86->operands[1];
    assert(src->access == CS_AC_READ);
    assert(src->type == X86_OP_MEM);
    assert(src->mem.segment == X86_REG_INVALID); // idc, flat model anyway
    // calculate the effective load addr

    unsigned long calc_addr;
    assert(src->mem.base != X86_REG_INVALID); // what kind of opcodes are these??
    calc_addr = u->uc_mcontext.gregs[capstone_reg_to_greg(handle, src->mem.base)];
    assert(src->mem.index == X86_REG_INVALID);
    assert(src->mem.scale == 1);
    calc_addr += src->mem.disp;
    if (src->mem.base == X86_REG_RIP) {
        calc_addr += n->size;
    }

    if (calc_addr != addr) {
        printf("calc_addr %lx addr %lx\n", calc_addr, addr);
    }
    assert(calc_addr == addr);

    // operand 1: dest
    cs_x86_op *dst = &x86->operands[0];
    assert(dst->access == CS_AC_WRITE);
    assert(dst->type == X86_OP_REG);
    // printf("read from %lx to register %s\n", calc_addr, cs_reg_name(handle, dst->reg));

    bool is_signed = false;
    switch (n->id) {
    case X86_INS_MOVZX:
        is_signed = true;
        break;

    case X86_INS_MOV:
    default:
        assert(dst->size == src->size);
        break;

    case X86_INS_MOVSX: break;
        break;
    }

    unsigned long value;
    switch (dst->size) {
    case 1: value = is_signed ? MP_PGM_ACCESS(*(int8_t*)calc_addr): MP_PGM_ACCESS(*(uint8_t*)calc_addr); break;
    case 2: value = is_signed ? MP_PGM_ACCESS(*(int16_t*)calc_addr): MP_PGM_ACCESS(*(uint16_t*)calc_addr); break;
    case 4: value = is_signed ? MP_PGM_ACCESS(*(int32_t*)calc_addr): MP_PGM_ACCESS(*(uint32_t*)calc_addr); break;
    case 8: value = is_signed ? MP_PGM_ACCESS(*(int64_t*)calc_addr): MP_PGM_ACCESS(*(uint64_t*)calc_addr); break;
    default: assert(!"invalid size");
    }

    u->uc_mcontext.gregs[capstone_reg_to_greg(handle, dst->reg)] = value;
    // skip the faulting instruction
    u->uc_mcontext.gregs[REG_RIP] += n->size;

    cs_close(&handle);
    progmem_printf("progmem access: instruction at %lx accessed %lx\n", pc, addr);
    // progmem_printf("progmem access: instruction at %lx accessed %lx\n", pc - text, addr);
    // progmem_printf("progmem access: instruction at %lx accessed %lx\n", pc - text + 0x17000, addr);
}

static void init_progmem(void) {
    progmem_fd = open("progmem.log", O_WRONLY);

    // TODO explain about these 2.
    static char start_xx __attribute__((section("progmem,\"a\",@progbits\n.align 0x1000#"))) = 0x1;
    static char end_xx __attribute__((section(".eh_frame_hdr.shit,\"a\",@progbits\n.align 0x1000#"))) = 0x1;
    progmem_printf("ftr %p %p\n", &start_xx, &end_xx);

    // remap the progmem area so memory accesses to MP_PROGMEM without MP_PGM_ACCESS
    // fail.
    unsigned char *spgm = &__start_progmem;
    unsigned char *epgm = &__stop_progmem;

    progmem_printf("remapping progmem %p - %p size %lu\n", spgm, epgm, epgm - spgm);

    unsigned long pgm_size = (unsigned long)epgm - (unsigned long)spgm;
    unsigned long new_pgm = (unsigned long)spgm + PROGMEM_OFFSET;
    if ((unsigned long)spgm & 0xfff) {
        progmem_printf("not aligned to page size!\n");
        exit(1);
    }
    // TODO pgm_size is not page aligned, but next section really does start on the next page
    // multiply. verify it somehow.

    // move progmem to its new location
    progmem_printf("new progmem is at %p\n", (void*)new_pgm);
    // not using mremap so older progmem remains in place (for debugging progmem copies)
    void *res = mmap((void*)new_pgm, pgm_size, PROT_WRITE, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
    if (res == MAP_FAILED) {
        perror("mmap new progmem");
        exit(1);
    }

    memcpy(res, spgm, pgm_size);

    // now make it read-only, as it should be.
    if (0 != mprotect(res, pgm_size, PROT_READ)) {
        perror("mprotect new pgmem");
        exit(1);
    }

    // and make accesses to the old location fail violently.
    if (0 != mprotect(spgm, pgm_size, PROT_NONE)) {
        perror("mprotect old pgmem");
        exit(1);
    }

    progmem_printf("progmem remapped!\n");

    struct sigaction sa = {
        .sa_flags = SA_SIGINFO,
        .sa_sigaction = sigsegv_handler,
    };
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    char path[128];
    sprintf(path, "/proc/%d/maps", getpid());
    FILE *f = fopen(path, "r");
    while (!feof(f)) {
        char line[1024];
        fgets(line, sizeof(line), f);

        if (strstr(line, "r-xp")) {
            *strchr(line, '-') = '\0';
            progmem_printf("text %s\n", line);
            text = strtoul(line, NULL, 16) + 0x850;
            break;
        }
    }
}
#endif

int main(int argc, char **argv) {
    #if MICROPY_PY_THREAD
    mp_thread_init();
    #endif
    // We should capture stack top ASAP after start, and it should be
    // captured guaranteedly before any other stack variables are allocated.
    // For this, actual main (renamed main_) should not be inlined into
    // this function. main_() itself may have other functions inlined (with
    // their own stack variables), that's why we need this main/main_ split.
    mp_stack_ctrl_init();

    #if MICROPY_UNIX_PROGMEM_TEST
    init_progmem();
    #endif

    return main_(argc, argv);
}

MP_NOINLINE int main_(int argc, char **argv) {
    #ifdef SIGPIPE
    // Do not raise SIGPIPE, instead return EPIPE. Otherwise, e.g. writing
    // to peer-closed socket will lead to sudden termination of MicroPython
    // process. SIGPIPE is particularly nasty, because unix shell doesn't
    // print anything for it, so the above looks like completely sudden and
    // silent termination for unknown reason. Ignoring SIGPIPE is also what
    // CPython does. Note that this may lead to problems using MicroPython
    // scripts as pipe filters, but again, that's what CPython does. So,
    // scripts which want to follow unix shell pipe semantics (where SIGPIPE
    // means "pipe was requested to terminate, it's not an error"), should
    // catch EPIPE themselves.
    signal(SIGPIPE, SIG_IGN);
    #endif

    mp_stack_set_limit(40000 * (BYTES_PER_WORD / 4));

    pre_process_options(argc, argv);

#if MICROPY_ENABLE_GC
    char *heap = malloc(heap_size);
    gc_init(heap, heap + heap_size);
#endif

    #if MICROPY_ENABLE_PYSTACK
    static mp_obj_t pystack[1024];
    mp_pystack_init(pystack, &pystack[MP_ARRAY_SIZE(pystack)]);
    #endif

    mp_init();

    #if MICROPY_EMIT_NATIVE
    // Set default emitter options
    MP_STATE_VM(default_emit_opt) = emit_opt;
    #else
    (void)emit_opt;
    #endif

    #if MICROPY_VFS_POSIX
    {
        // Mount the host FS at the root of our internal VFS
        mp_obj_t args[2] = {
            mp_type_vfs_posix.make_new(&mp_type_vfs_posix, 0, 0, NULL),
            MP_OBJ_NEW_QSTR(MP_QSTR__slash_),
        };
        mp_vfs_mount(2, args, (mp_map_t*)&mp_const_empty_map);
        MP_STATE_VM(vfs_cur) = MP_STATE_VM(vfs_mount_table);
    }
    #endif

    char *home = getenv("HOME");
    char *path = getenv("MICROPYPATH");
    if (path == NULL) {
        #ifdef MICROPY_PY_SYS_PATH_DEFAULT
        path = MICROPY_PY_SYS_PATH_DEFAULT;
        #else
        path = "~/.micropython/lib:/usr/lib/micropython";
        #endif
    }
    size_t path_num = 1; // [0] is for current dir (or base dir of the script)
    if (*path == ':') {
        path_num++;
    }
    for (char *p = path; p != NULL; p = strchr(p, PATHLIST_SEP_CHAR)) {
        path_num++;
        if (p != NULL) {
            p++;
        }
    }
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_path), path_num);
    mp_obj_t *path_items;
    mp_obj_list_get(mp_sys_path, &path_num, &path_items);
    path_items[0] = MP_OBJ_NEW_QSTR(MP_QSTR_);
    {
    char *p = path;
    for (mp_uint_t i = 1; i < path_num; i++) {
        char *p1 = strchr(p, PATHLIST_SEP_CHAR);
        if (p1 == NULL) {
            p1 = p + strlen(p);
        }
        if (p[0] == '~' && p[1] == '/' && home != NULL) {
            // Expand standalone ~ to $HOME
            int home_l = strlen(home);
            vstr_t vstr;
            vstr_init(&vstr, home_l + (p1 - p - 1) + 1);
            vstr_add_strn(&vstr, home, home_l);
            vstr_add_strn(&vstr, p + 1, p1 - p - 1);
            path_items[i] = mp_obj_new_str_from_vstr(&mp_type_str, &vstr);
        } else {
            path_items[i] = mp_obj_new_str_via_qstr(p, p1 - p);
        }
        p = p1 + 1;
    }
    }

    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);

    #if defined(MICROPY_UNIX_COVERAGE)
    {
        MP_DECLARE_CONST_FUN_OBJ_0(extra_coverage_obj);
        mp_store_global(QSTR_FROM_STR_STATIC("extra_coverage"), MP_OBJ_FROM_PTR(&extra_coverage_obj));
    }
    #endif

    // Here is some example code to create a class and instance of that class.
    // First is the Python, then the C code.
    //
    // class TestClass:
    //     pass
    // test_obj = TestClass()
    // test_obj.attr = 42
    //
    // mp_obj_t test_class_type, test_class_instance;
    // test_class_type = mp_obj_new_type(QSTR_FROM_STR_STATIC("TestClass"), mp_const_empty_tuple, mp_obj_new_dict(0));
    // mp_store_name(QSTR_FROM_STR_STATIC("test_obj"), test_class_instance = mp_call_function_0(test_class_type));
    // mp_store_attr(test_class_instance, QSTR_FROM_STR_STATIC("attr"), mp_obj_new_int(42));

    /*
    printf("bytes:\n");
    printf("    total %d\n", m_get_total_bytes_allocated());
    printf("    cur   %d\n", m_get_current_bytes_allocated());
    printf("    peak  %d\n", m_get_peak_bytes_allocated());
    */

    const int NOTHING_EXECUTED = -2;
    int ret = NOTHING_EXECUTED;
    bool inspect = false;
    for (int a = 1; a < argc; a++) {
        if (argv[a][0] == '-') {
            if (strcmp(argv[a], "-i") == 0) {
                inspect = true;
            } else if (strcmp(argv[a], "-c") == 0) {
                if (a + 1 >= argc) {
                    return usage(argv);
                }
                ret = do_str(argv[a + 1]);
                if (ret & FORCED_EXIT) {
                    break;
                }
                a += 1;
            } else if (strcmp(argv[a], "-m") == 0) {
                if (a + 1 >= argc) {
                    return usage(argv);
                }
                mp_obj_t import_args[4];
                import_args[0] = mp_obj_new_str(argv[a + 1], strlen(argv[a + 1]));
                import_args[1] = import_args[2] = mp_const_none;
                // Ask __import__ to handle imported module specially - set its __name__
                // to __main__, and also return this leaf module, not top-level package
                // containing it.
                import_args[3] = mp_const_false;
                // TODO: https://docs.python.org/3/using/cmdline.html#cmdoption-m :
                // "the first element of sys.argv will be the full path to
                // the module file (while the module file is being located,
                // the first element will be set to "-m")."
                set_sys_argv(argv, argc, a + 1);

                mp_obj_t mod;
                nlr_buf_t nlr;
                bool subpkg_tried = false;

            reimport:
                if (nlr_push(&nlr) == 0) {
                    mod = mp_builtin___import__(MP_ARRAY_SIZE(import_args), import_args);
                    nlr_pop();
                } else {
                    // uncaught exception
                    return handle_uncaught_exception(nlr.ret_val) & 0xff;
                }

                if (mp_obj_is_package(mod) && !subpkg_tried) {
                    subpkg_tried = true;
                    vstr_t vstr;
                    int len = strlen(argv[a + 1]);
                    vstr_init(&vstr, len + sizeof(".__main__"));
                    vstr_add_strn(&vstr, argv[a + 1], len);
                    vstr_add_strn(&vstr, ".__main__", sizeof(".__main__") - 1);
                    import_args[0] = mp_obj_new_str_from_vstr(&mp_type_str, &vstr);
                    goto reimport;
                }

                ret = 0;
                break;
            } else if (strcmp(argv[a], "-X") == 0) {
                a += 1;
            #if MICROPY_DEBUG_PRINTERS
            } else if (strcmp(argv[a], "-v") == 0) {
                mp_verbose_flag++;
            #endif
            } else if (strncmp(argv[a], "-O", 2) == 0) {
                if (unichar_isdigit(argv[a][2])) {
                    MP_STATE_VM(mp_optimise_value) = argv[a][2] & 0xf;
                } else {
                    MP_STATE_VM(mp_optimise_value) = 0;
                    for (char *p = argv[a] + 1; *p && *p == 'O'; p++, MP_STATE_VM(mp_optimise_value)++);
                }
            } else {
                return usage(argv);
            }
        } else {
            char *pathbuf = malloc(PATH_MAX);
            char *basedir = realpath(argv[a], pathbuf);
            if (basedir == NULL) {
                mp_printf(&mp_stderr_print, "%s: can't open file '%s': [Errno %d] %s\n", argv[0], argv[a], errno, strerror(errno));
                // CPython exits with 2 in such case
                ret = 2;
                break;
            }

            // Set base dir of the script as first entry in sys.path
            char *p = strrchr(basedir, '/');
            path_items[0] = mp_obj_new_str_via_qstr(basedir, p - basedir);
            free(pathbuf);

            set_sys_argv(argv, argc, a);
            ret = do_file(argv[a]);
            break;
        }
    }

    if (ret == NOTHING_EXECUTED || inspect) {
        if (isatty(0)) {
            prompt_read_history();
            ret = do_repl();
            prompt_write_history();
        } else {
            ret = execute_from_lexer(LEX_SRC_STDIN, NULL, MP_PARSE_FILE_INPUT, false);
        }
    }

    #if MICROPY_PY_SYS_SETTRACE
    MP_STATE_THREAD(prof_trace_callback) = MP_OBJ_NULL;
    #endif

    #if MICROPY_PY_SYS_ATEXIT
    // Beware, the sys.settrace callback should be disabled before running sys.atexit.
    if (mp_obj_is_callable(MP_STATE_VM(sys_exitfunc))) {
        mp_call_function_0(MP_STATE_VM(sys_exitfunc));
    }
    #endif

    #if MICROPY_PY_MICROPYTHON_MEM_INFO
    if (mp_verbose_flag) {
        mp_micropython_mem_info(0, NULL);
    }
    #endif

    #if MICROPY_PY_THREAD
    mp_thread_deinit();
    #endif

    #if defined(MICROPY_UNIX_COVERAGE)
    gc_sweep_all();
    #endif

    mp_deinit();

#if MICROPY_ENABLE_GC && !defined(NDEBUG)
    // We don't really need to free memory since we are about to exit the
    // process, but doing so helps to find memory leaks.
    free(heap);
#endif

    //printf("total bytes = %d\n", m_get_total_bytes_allocated());
    return ret & 0xff;
}

#if !MICROPY_VFS
uint mp_import_stat(const char *path) {
    struct stat st;
    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return MP_IMPORT_STAT_DIR;
        } else if (S_ISREG(st.st_mode)) {
            return MP_IMPORT_STAT_FILE;
        }
    }
    return MP_IMPORT_STAT_NO_EXIST;
}
#endif

void nlr_jump_fail(void *val) {
    printf("FATAL: uncaught NLR %p\n", val);
    exit(1);
}
