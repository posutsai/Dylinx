CC=clang
.PHONY: clean
INCLUDE_FLAG=-I/usr/local/lib/clang/$(shell clang -dumpversion)/include -I${DYLINX_HOME}/src/glue -I${DYLINX_HOME}/sample/hot-exp/.dylinx/glue
LD_FLAG=-L${DYLINX_HOME}/build/lib -L${DYLINX_HOME}/sample/hot-exp/.dylinx/lib -lpthread -lm
ifeq ($(with_dlx), 1)
DYLINX_FLAG=-ldl -ldlx-init -latomic -ldlx-glue
else
DYLINX_FLAG=-ldl -latomic
endif

ratio-measure: src/ratio-measure.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DCS_RATIO=${cs_ratio} -DCS_DURATION_EXP=${cs_exp} -DTEST_DURATION -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) -fxray-instrument

single-hot: src/single-hot.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DWITH_MUTEX -DCS_RATIO=${cs_ratio} -DCS_DURATION_EXP=${cs_exp} -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) $(DYLINX_FLAG) -fxray-instrument

lockless: src/single-hot.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DCS_RATIO=${cs_ratio} -DCS_DURATION_EXP=${cs_exp} -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) $(DYLINX_FLAG) -fxray-instrument

lock-overhead: src/machine-repair.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DWITH_MUTEX -DCS_RATIO=${cs_ratio} -DN_CORE=${n_core} -DNUM_THREAD=${num_thread} -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) $(DYLINX_FLAG) -fxray-instrument

ideal-measure: src/machine-repair.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -DCS_RATIO=${cs_ratio} -DN_CORE=${n_core} -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) $(DYLINX_FLAG) -fxray-instrument


clean:
	/bin/rm -rf bin/*
	/bin/rm -rf xray-log/*
