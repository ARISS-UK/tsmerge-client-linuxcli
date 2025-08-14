
BIN = tsmerge-client-linuxcli

CC = gcc
CFLAGS = -std=gnu11 -D_GNU_SOURCE

GCCVERSION_GTEQ_10 := $(shell expr `gcc -dumpversion | cut -f1 -d.` \>= 10)
ifeq "$(GCCVERSION_GTEQ_10)" "1"
    CFLAGS += -fanalyzer
endif

CFLAGS += -O2 -ggdb -Wall -Wextra -Wpedantic -Wunused -Werror -D_FORTIFY_SOURCE=2 -fstack-protector-strong
CFLAGS += -D BUILD_VERSION="\"$(shell git describe --dirty --always 2>/dev/null || echo 'untracked')\""	\
		-D BUILD_DATE="\"$(shell date '+%Y-%m-%d %H:%M:%S')\""
CFLAGS += -pthread

LIBFLAGS = 

SRCDIR = ./src/
SRCS = ${SRCDIR}/main.c \
		${SRCDIR}/ts/ts.c \
		${SRCDIR}/util/udp.c \
		${SRCDIR}/util/timing.c

all: $(BIN)

$(BIN):
	$(CC) $(CFLAGS) $(LIBFLAGS) $(SRCS) -I $(SRCDIR) -o $(BIN)

clean:
	rm -fv *.o $(BIN)

cppcheck:
	@cppcheck -j 4 \
		--enable=warning \
		--enable=performance \
		--enable=portability \
		--cppcheck-build-dir=./.cppcheck/ \
		$(SRCDIR)
