CC=clang
.PHONY: clean
INCLUDE_FLAG=-I/usr/local/lib/clang/$(shell clang -dumpversion)/include -I${DYLINX_HOME}/src/glue -I${DYLINX_HOME}/sample/pipe/.dylinx/glue
LD_FLAG=-L${DYLINX_HOME}/build/lib -L${DYLINX_HOME}/sample/pipe/.dylinx/lib -lpthread -lm
ifeq ($(with_dlx), 1)
DYLINX_FLAG=-ldl -ldlx-init -latomic -ldlx-glue
else
DYLINX_FLAG=-ldl -latomic
endif

queue: pipe.c main.c
ifndef n_thread
	$(error n_thread is a required argument!!!)
endif
	$(CC) $^ -DN_THREAD=$(n_thread) -o bin/$@ $(INCLUDE_FLAG) $(LD_FLAG) $(DYLINX_FLAG) -fxray-instrument

clean:
	/bin/rm -rf bin/*
	/bin/rm -rf xray-log/*
