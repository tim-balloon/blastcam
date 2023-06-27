all: release

release: commands.c commands.h camera.c camera.h lens_adapter.c lens_adapter.h astrometry.c astrometry.h matrix.c matrix.h
	gcc commands.c camera.c lens_adapter.c matrix.c astrometry.c -lsofa -lpthread -lastrometry -lueye_api -lm -o commands

debug: commands.c commands.h camera.c camera.h lens_adapter.c lens_adapter.h astrometry.c astrometry.h matrix.c matrix.h
	gcc -g -Og commands.c camera.c lens_adapter.c matrix.c astrometry.c -lsofa -lpthread -lastrometry -lueye_api -lm -o commands

.PHONY: clean

clean:
	rm -f *.o test_camera
