CFLAGS=-Wall -Wextra -lSDL2 -lpthread

all: terminominal

terminominal: main_sdl.o sdlgui.o terminal.o eia_linux.o error.o char.o
	gcc ${CFLAGS} $^ -o $@

main_sdl.o: main_sdl.c
	gcc ${CFLAGS} -c $^ -o $@

sdlgui.o: sdlgui.c
	gcc ${CFLAGS} -c $^ -o $@

terminal.o: terminal.c
	gcc ${CFLAGS} -c $^ -o $@

eia_linux.o: eia_linux.c
	gcc ${CFLAGS} -c $^ -o $@

error.o: error.c
	gcc ${CFLAGS} -c $^ -o $@

char.o: char.rom
	objcopy -I binary -O elf64-x86-64 -B i386 $^ $@

.PHONY: clean
clean:
	rm -f *.o terminominal

