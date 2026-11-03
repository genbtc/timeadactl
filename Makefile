.POSIX:

CC = cc
CFLAGS = -std=c11 -O2 -Wall -Wextra -Wpedantic
LDFLAGS =
LDLIBS = -lm

PREFIX = /usr/local
BINDIR = $(PREFIX)/bin

all: timeadactl

timeadactl: timeadactl.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ timeadactl.c $(LDLIBS)

check: timeadactl
	./timeadactl --help >/dev/null
	@echo "Build and CLI smoke test passed."

install: timeadactl
	mkdir -p "$(DESTDIR)$(BINDIR)"
	install -m 0755 timeadactl "$(DESTDIR)$(BINDIR)/timeadactl"

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/timeadactl"

clean:
	rm -f timeadactl

.PHONY: all check install uninstall clean
