CC=clang
.PHONY: clean
INCLUDE_FLAG=-I/usr/local/lib/clang/$(shell clang -dumpversion)/include -I${DYLINX_HOME}/src/glue -I${DYLINX_HOME}/sample/hot-exp/.dylinx/glue
LD_FLAG=-L${DYLINX_HOME}/build/lib -L${DYLINX_HOME}/sample/hot-exp/.dylinx/lib -lpthread -lm
DYLINX_FLAG=-ldl -ldlx-init -latomic

contentionless: poisson-process.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DCS_RATIO=${cs_ratio} -DCS_DURATION_EXP=${cs_exp} -DTEST_DURATION -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) -fxray-instrument

single-hot: poisson-process.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DWITH_MUTEX -DCS_RATIO=${cs_ratio} -DCS_DURATION_EXP={cs_exp} -O0 -fxray-instrument -fxray-instruction-threshold=1
	$(CC) bin/$@.o ${DYLINX_HOME}/build/.objs/dlx-glue/linux/x86_64/release/src/glue/dylinx-glue.c.o -o bin/$@-${ltype} $(LD_FLAG) $(DYLINX_FLAG) -fxray-instrument -fxray-instruction-threshold=1

clean:
	/bin/rm -rf bin/*
	/bin/rm -rf xray-log/*
