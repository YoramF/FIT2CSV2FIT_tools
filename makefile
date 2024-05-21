fit2csv:	fit2csv.o fit_titles.o ../FIT_SDK/libfit.a
	gcc -s -o fit2csv.exe fit2csv.o fit_titles.o -lfit -L../FIT_SDK

fit2csv.o:	fit2csv.c fit_titles.c fit_titles.h
	gcc -o fit2csv.o -c -O3 fit2csv.c -I../FIT_SDK/src -I.
	gcc -o fit_titles.o -c -O3 fit_titles.c -I../FIT_SDK/src -I.

fit2csv_d:	fit2csv_d.o fit_titles_d.o ../FIT_SDK/libfit_d.a
	gcc -o fit2csv_debug.exe fit2csv_d.o fit_titles_d.o -lfit_d -L../FIT_SDK

fit2csv_d.o:	fit2csv.c fit_titles.c fit_titles.h
	gcc -o fit2csv_d.o -c -g fit2csv.c -I../FIT_SDK/src -I.
	gcc -o fit_titles_d.o -c -g fit_titles.c -I../FIT_SDK/src -I.

csv2fit:	csv2fit.o ../FIT_SDK/libfit.a
	gcc -s -o csv2fit.exe csv2fit.o -lfit -L../FIT_SDK

csv2fit.o:	csv2fit.c
	gcc -o csv2fit.o -c -O3 csv2fit.c -I../FIT_SDK/src

csv2fit_d:	csv2fit_d.o ../FIT_SDK/libfit_d.a
	gcc -o csv2fit_debug.exe csv2fit_d.o -lfit_d -L../FIT_SDK

csv2fit_d.o:	csv2fit.c
	gcc -o csv2fit_d.o -c -g csv2fit.c -I../FIT_SDK/src

clean:
	rm *.o *.exe