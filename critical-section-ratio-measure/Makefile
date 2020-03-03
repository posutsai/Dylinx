CXX=clang++-9
CC=clang-9
OPT=opt-9
CXX_FLAGS=`llvm-config-9 --cxxflags`
LD_FLAGS=`llvm-config-9 --ldflags --libs all`
OPT_PASS_FLAGS= -pthread-detect
LOAD_FILES= -load bin/pthreadDetectPass.so

.PHONY: all clean run-pass

pthreadDetectPass: pthreadDetectPass.cpp
	$(CXX) -fPIC -c pthreadDetectPass.cpp -o bin/$@.o $(CXX_FLAGS)
	$(CXX) -shared -o bin/$@.so bin/$@.o $(LD_FLAGS)

target: target.cpp
	$(CC) -g -emit-llvm target.cpp -c -o bin/target.bc

run-pass: pthreadDetectPass target
	$(OPT) $(LOAD_FILES) $(OPT_PASS_FLAGS) < bin/target.bc > /dev/null


