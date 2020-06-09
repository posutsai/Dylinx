CC=clang
.PHONY: clean
INCLUDE_FLAG=-I/usr/local/lib/clang/10.0.0/include
LD_FLAG=-L${DYLINX_GLUE_PATH} -ldlx-glue

sample: main.o func.o
	$(CC) -o build/$@ build/*.o $(LD_FLAG)

%.o: %.c
	$(CC) -c $^ -o build/$@ $(INCLUDE_FLAG)

clean:
	/bin/rm -rf build/*
