CC=clang
.PHONY: clean
INCLUDE_FLAG=-I/usr/local/lib/clang/$(shell clang -dumpversion)/include -I${DYLINX_HOME}/src/glue -I${DYLINX_HOME}/sample/colony/.dylinx/glue
LD_FLAG=-L${DYLINX_HOME}/build/lib -L${DYLINX_HOME}/sample/colony/.dylinx/lib -lpthread -lm
ifeq ($(with_dlx), 1)
DYLINX_FLAG=-ldl -ldlx-init -latomic -ldlx-glue
else
DYLINX_FLAG=-ldl -latomic
endif

colony-inspect: colony-inspect.c
	$(CC) -c $^ -o bin/$@.o $(INCLUDE_FLAG) -O0 -fxray-instrument
	$(CC) bin/$@.o -o bin/$@ $(LD_FLAG) $(DYLINX_FLAG) -fxray-instrument

clean:
	/bin/rm -rf bin/*
	/bin/rm -rf xray-log/*
