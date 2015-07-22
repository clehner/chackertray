BIN = chackertray
SRC = chackertray.c
OBJ = $(SRC:.c=.o)
CFLAGS = -Wall -pedantic -std=gnu99
CFLAGS = $(shell pkg-config --cflags gtk+-2.0)
LDFLAGS = $(shell pkg-config --libs gtk+-2.0)

all: $(BIN)

debug:
	$(MAKE) --no-print-directory CFLAGS+=-g
	gdb ./$(BIN)

$(BIN):: $(OBJ)
	$(CC) -o $@ $^ $(LDFLAGS)

.c.o:
	$(CC) -c $(CFLAGS) $< -o $@

install: all
	install -m 0755 $(BIN) $(DESTDIR)$(BINDIR)

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(BIN)

clean:
	rm -f $(BIN) $(OBJ)

.PHONY: all install uninstall
