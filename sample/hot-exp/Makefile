CC=clang
.PHONY: clean
INCLUDE_FLAG=-I/usr/local/lib/clang/10.0.0/include -I${DYLINX_HOME}/src/glue -I${DYLINX_HOME}/sample/exp/.dylinx/glue
LD_FLAG=-L${DYLINX_HOME}/build/lib -L${DYLINX_HOME}/sample/hot-exp/.dylinx/lib -lpthread -lm
DYLINX_FLAG=-ldl -ldlx-init -latomic

contentionless: poisson-process.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DCS_RATIO=${cs_ratio} -DTEST_DURATION -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) -fxray-instrument

hot-exp: poisson-process.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DCS_RATIO=${cs_ratio} -DWITH_MUTEX -O0
	$(CC) bin/$@.o -o bin/$@-process $(LD_FLAG)

clean:
	/bin/rm -rf bin/*
