# FIT2CSV2FIT_tools
Convert FIT file to CSV and vice versa. 

There are several tools to convert FIT files to CSV and vice versa. I could not find a simple free tool that I can use, so, I wrote my own tool.
The tool is using GARMIN FIT SDK. The published SDK does not include all message type and definitions, so I could not use the provided FIT_*() 
functions in the SDK and instead, I just used the constants and definitioons in the include files and wrote my own file scanning procedure.
The generated CSV file contains mainly numeric values to overcome the missing definitions, and to allow easy conversion back to FIT.
The reson to convert a CSV file back to FIT is to allow changing values in FIT file is required (for instance - update total distance in Totals.fit, in case of upgrading
the GPS unit).

To generate the GARMIN FIT SDK C library you need to fetch the sources form 
https://developer.garmin.com/downloads/fit/sdk/FitSDKRelease_21.133.00.zip.
Extract all c and h files.

complie all c files and add all objects file to libfit.a library file.

Here is the makefile I am using to build the fit library for windows:

CFILES = $(wildcard ./src/*.c)
OFILES = $(CFILES:./src/%.c=./obj/%.o)
OFILES_d = $(CFILES:./src/%.c=./obj/%_d.o)
COMPILEFLAGS = -O3
COMPILEFLAGS_d = -g

all: clean libfit.a libfit_d.a

libfit: $(OFILES)
	ar rcs libfit.a $^

./obj/%.o: ./src/%.c ./src/%.h
	gcc $(COMPILEFLAGS) -o $@ -c $<

libfit_d: $(OFILES_d)
	ar rcs libfit_d.a $^

./obj/%_d.o: ./src/%.c ./src/%.h
	gcc $(COMPILEFLAGS_d) -o $@ -c $<


clean:
	rm ./obj/*.o
