CC=clang
.PHONY: clean
INCLUDE_FLAG=-I/usr/local/lib/clang/10.0.0/include -I${DYLINX_GLUE_PATH}/src/glue
LD_FLAG=-L${DYLINX_GLUE_PATH}/build/lib -ldlx-glue -lpthread

sample: main.o
	$(CC) -o bin/$@ bin/*.o $(LD_FLAG)

%.o: %.c
	$(CC) -c $^ -o bin/$@ $(INCLUDE_FLAG)

clean:
	/bin/rm -rf bin/*
