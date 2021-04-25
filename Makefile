LIB_NAME := rkv

CFLAGS := -pthread -std=c11 -I inc -I lib -fvisibility=hidden -D_DEFAULT_SOURCE=__STRICT_ANSI__ -D_FORTIFY_SOURCE=2 -fPIC\
 -pedantic -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wcast-align -Wcast-qual -Wdisabled-optimization -Wlogical-op\
 -Wmissing-declarations -Wmissing-include-dirs -Wredundant-decls -Wshadow -Wsign-conversion -Wswitch-default -Wundef\
 -Wwrite-strings -Wfloat-equal -fmessage-length=0

SRCS :=\
 src/rkv.c\
 src/rkv_id.c

SRCS_TST :=\
 test/main.c\
 test/rkv_test.c

OBJS         := $(SRCS:%c=BUILD/%o)
OBJS_DBG     := $(SRCS:%c=BUILD/DEBUG/%o)
OBJS_DBG_TST := $(SRCS_TST:%c=BUILD/DEBUG/%o)
DEPS         := $(SRCS:%c=BUILD/%d)
DEPS_TST     := $(SRCS_TST:%c=BUILD/%d)

VALGRIND_COMMON_OPTIONS :=\
# --track-fds=yes
 --error-exitcode=2

VALGRIND_MEMCHECK_OPTIONS := $(VALGRIND_COMMON_OPTIONS)\
 --leak-check=full\
 --show-leak-kinds=all\
 --track-origins=yes\
 --expensive-definedness-checks=yes

VALGRIND_HELGRIND_OPTIONS := $(VALGRIND_COMMON_OPTIONS)\
 --free-is-write=yes

.PHONY: all validate memcheck helgrind clean

all: lib$(LIB_NAME).so lib$(LIB_NAME)-d.so tests-d

validate: tests-d
	@g++ test/cplusplus_ckeck.cpp -c -I inc -I lib -W -Wall -pedantic
	@rm cplusplus_ckeck.o
	LD_LIBRARY_PATH=.:../utils ./tests-d

memcheck: tests-d
	LD_LIBRARY_PATH=.:../utils valgrind --tool=memcheck $(VALGRIND_MEMCHECK_OPTIONS) ./tests-d

helgrind: tests-d
	LD_LIBRARY_PATH=.:../utils valgrind --tool=helgrind $(VALGRIND_HELGRIND_OPTIONS) ./tests-d

clean:
	rm -fr BUILD bin depcache build Debug Release
	rm -f lib$(LIB_NAME)-d.so lib$(LIB_NAME).so tests-d

lib$(LIB_NAME).so: $(OBJS)
	gcc $^ -shared -o $@
	strip --discard-all --discard-locals $@

lib$(LIB_NAME)-d.so: $(OBJS_DBG)
	gcc $^ -shared -o $@

tests-d: $(OBJS_DBG_TST) lib$(LIB_NAME)-d.so
	gcc $(OBJS_DBG_TST) -o $@ -pthread -L. -l$(LIB_NAME)-d -L../utils -lutils-d

BUILD/%.o: %.c
	@mkdir -p $$(dirname $@)
	gcc $(CFLAGS) -O3 -g0 -c -MMD -MP -MF"$(@:%.o=%.d)" -MT $@ -o $@ $<

BUILD/DEBUG/%.o: %.c
	@mkdir -p $$(dirname $@)
	gcc $(CFLAGS) -O0 -g3 -c -MMD -MP -MF"$(@:%.o=%.d)" -MT $@ -o $@ $<

-include $(DEPS) $(DEPS_TST)
