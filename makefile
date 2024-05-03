fit2csv:	fit2csv.o ../FIT_SDK/libfit.a
	gcc -s -o fit2csv.exe fit2csv.o -lfit -L../FIT_SDK

fit2csv.o:	fit2csv.c
	gcc -o fit2csv.o -c -O3 fit2csv.c -I../FIT_SDK/src

fit2csv_d:	fit2csv_d.o ../FIT_SDK/libfit_d.a
	gcc -o fit2csv_debug.exe fit2csv_d.o -lfit_d -L../FIT_SDK

fit2csv_d.o:	fit2csv.c
	gcc -o fit2csv_d.o -c -g fit2csv.c -I../FIT_SDK/src

clean:
	rm *.o *.exe