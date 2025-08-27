all: release

release: sc_commander.c
	gcc sc_commander.c -lpthread -lm -o commander

debug: sc_commander.c
	gcc -g -Og sc_commander.c -lpthread -lm -o commander

.PHONY: clean

clean:
	rm -f *.o commander
