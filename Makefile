CFLAGS = -pthread -std=c11 -I src-c -fvisibility=hidden -D_DEFAULT_SOURCE=__STRICT_ANSI__ -D_FORTIFY_SOURCE=2 -fPIC\
 -pedantic -pedantic-errors -Wall -Wextra -Werror -Wconversion -Wcast-align -Wcast-qual -Wdisabled-optimization -Wlogical-op\
 -Wmissing-declarations -Wmissing-include-dirs -Wredundant-decls -Wshadow -Wsign-conversion -Wswitch-default -Wundef\
 -Wwrite-strings -Wfloat-equal -fmessage-length=0

SRCS_LIB =\
 src/rkv.c\
 src/net_buff.c\
 src/map_of_strings.c

SRCS_EXE = src/main.c
SRCS_TST =\
 test/main.c\
 test/rkv_test.c\
 test/tests_utils.c

OBJS_LIB     = $(SRCS_LIB:%c=BUILD/%o)
OBJS_EXE     = $(SRCS_EXE:%c=BUILD/%o)
OBJS_DBG_LIB = $(SRCS_LIB:%c=BUILD/DEBUG/%o)
OBJS_DBG_EXE = $(SRCS_EXE:%c=BUILD/DEBUG/%o)
OBJS_DBG_TST = $(SRCS_TST:%c=BUILD/DEBUG/%o)
DEPS_LIB     = $(SRCS_LIB:%c=BUILD/%d)
DEPS_EXE     = $(SRCS_EXE:%c=BUILD/%d)
DEPS_TST     = $(SRCS_TST:%c=BUILD/%d)

.PHONY: all
all: libchat-d.so libchat.so chat-d chat tests-d

.PHONY: validate
validate: tests-d
	@LD_LIBRARY_PATH=. ./tests-d

.PHONY: validate-valgrind
validate-valgrind: tests-d
	LD_LIBRARY_PATH=. valgrind --leak-check=full --show-leak-kinds=all ./tests-d

.PHONY: clean
clean:
	rm -fr BUILD bin depcache build Debug Release
	rm -f libchat-d.so libchat.so chat-d chat tests-d

libchat.so: $(OBJS_LIB)
	gcc $^ -shared -o $@
	strip --discard-all --discard-locals $@

libchat-d.so: $(OBJS_DBG_LIB)
	gcc $^ -shared -o $@

chat: $(OBJS_EXE) libchat.so
	gcc $(OBJS_EXE) -o $@ -pthread -L. -lchat

chat-d: $(OBJS_DBG_EXE) libchat-d.so
	gcc $(OBJS_DBG_EXE) -o $@ -pthread -L. -lchat-d

tests-d: $(OBJS_DBG_TST) libchat-d.so
	gcc $(OBJS_DBG_TST) -o $@ -pthread -L. -lchat-d

BUILD/%.o: %.c
	@mkdir -p $$(dirname $@)
	gcc $(CFLAGS) -O3 -g0 -c -MMD -MP -MF"$(@:%.o=%.d)" -MT $@ -o $@ $<

BUILD/DEBUG/%.o: %.c
	@mkdir -p $$(dirname $@)
	gcc $(CFLAGS) -O0 -g3 -c -MMD -MP -MF"$(@:%.o=%.d)" -MT $@ -o $@ $<

-include $(DEPS_EXE) $(DEPS_TST)
