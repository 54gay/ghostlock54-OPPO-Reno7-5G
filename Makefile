all: ghostlock54

su: su.c
	clang -O3 -march=armv8.2-a+crc su.c -o su

su_bin.h: su
	@echo "[*] Packing su binary into C header..."
	@python3 -c "d=open('su','rb').read(); f=open('su_bin.h','w'); f.write('static const unsigned char su_bin[] = {' + ','.join(str(x) for x in d) + '};\n'); f.write('static const unsigned int su_bin_len = %d;\n' % len(d)); f.close()"

ghostlock54: ghostlock54.c su_bin.h
	clang -O3 -march=armv8.2-a+crc -pthread ghostlock54.c -o ghostlock54

clean:
	rm -rf ghostlock54 su_bin.h su

.PHONY: all clean
