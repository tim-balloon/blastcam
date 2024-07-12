all: release

release: commands.c commands.h camera.c camera.h lens_adapter.c lens_adapter.h astrometry.c astrometry.h matrix.c matrix.h sc_send.c sc_send.h sc_listen.c sc_listen.h sc_data_structures.h
	gcc commands.c camera.c lens_adapter.c matrix.c astrometry.c sc_listen.c sc_send.c -lsofa -lpthread -lastrometry -lueye_api -lm -o commands

debug: commands.c commands.h camera.c camera.h lens_adapter.c lens_adapter.h astrometry.c astrometry.h matrix.c matrix.h sc_send.c sc_send.h sc_listen.c sc_listen.h sc_data_structures.h
	gcc -g -Og commands.c camera.c lens_adapter.c matrix.c astrometry.c sc_listen.c sc_send.c -lsofa -lpthread -lastrometry -lueye_api -lm -o commands

.PHONY: clean

clean:
	rm -f *.o commands
