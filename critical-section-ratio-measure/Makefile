CXX=clang++-10
CC=clang-10
OPT=opt-10
PASS_CXX_FLAGS=`llvm-config-10 --cxxflags`
PASS_LD_FLAGS=`llvm-config-10 --ldflags --libs all`
BUILD_LD_FLAGS= -lpthread
OPT_PASS_FLAGS= -pthread-detect
LOAD_FILES= -load bin/pthreadDetectPass.so

.PHONY: all clean run-pass

pthreadDetectPass: pthreadDetectPass.cpp
	$(CXX) -fPIC -c pthreadDetectPass.cpp -o bin/$@.o $(PASS_CXX_FLAGS)
	$(CXX) -shared -o bin/$@.so bin/$@.o $(PASS_LD_FLAGS)

target: target.cpp
	$(CC) -S -emit-llvm target.cpp -o bin/$@.ll

run-pass: pthreadDetectPass target
	$(OPT) $(LOAD_FILES) $(OPT_PASS_FLAGS) -S < bin/target.ll > bin/target_optimized.ll

# The third argument of pthread_create: void *(*start_routine) (void *) has chances
# defined in other compilation unit. In that case, we have to make sure start_routine
# is able to access the global duration variable.
test-external-link: run-pass utils.cpp
	$(CXX) -c bin/target_optimized.ll -o bin/target_optimized.o
	$(CXX) -c utils.cpp -o bin/utils.o
	$(CXX) bin/utils.o bin/target_optimized.o $(BUILD_LD_FLAGS) -o bin/optimized

clean:
	/bin/rm -rf bin/*
