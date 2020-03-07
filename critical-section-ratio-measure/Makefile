CXX=clang++-10
CC=clang-10
OPT=opt-10
CXX_FLAGS=`llvm-config-10 --cxxflags`
LD_FLAGS=`llvm-config-10 --ldflags --libs all`
OPT_PASS_FLAGS= -pthread-detect
LOAD_FILES= -load bin/pthreadDetectPass.so

.PHONY: all clean run-pass

pthreadDetectPass: pthreadDetectPass.cpp
	$(CXX) -fPIC -c pthreadDetectPass.cpp -o bin/$@.o $(CXX_FLAGS)
	$(CXX) -shared -o bin/$@.so bin/$@.o $(LD_FLAGS)

target: target.cpp
	$(CC) -S -emit-llvm target.cpp -o bin/$@.ll

run-pass: pthreadDetectPass target
	$(OPT) $(LOAD_FILES) $(OPT_PASS_FLAGS) -S < bin/target.ll > bin/target_optimized.ll


