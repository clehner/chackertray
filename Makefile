BIN = chackertray
SRC = chackertray.c gmulticurl.c
OBJ = $(SRC:.c=.o)
CFLAGS = -Wall -Werror -Wextra -Wno-unused-parameter -pedantic -std=gnu99 -g
LDFLAGS = $(shell pkg-config --libs gtk+-2.0 libcurl)
PREFIX ?= /usr/local
BINDIR = $(PREFIX)/bin

all: $(BIN)

debug:
	@$(MAKE) --no-print-directory
	gdb ./$(BIN)

$(BIN):: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

chackertray.o: CFLAGS += $(shell pkg-config --cflags gtk+-2.0)
gmulticurl.o: CFLAGS += $(shell pkg-config --cflags glib-2.0)

install: all
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all install uninstall
