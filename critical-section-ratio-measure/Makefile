CXX=clang++
CC=clang-10
PASS_CXX_FLAGS=`llvm-config --cxxflags`
PASS_LD_FLAGS=`llvm-config --ldflags --libs --system-libs`
BUILD_LD_FLAGS= -L./bin -lmeasure -lpthread
OPT_PASS_FLAGS= -pthread-detect
LOAD_FILES= -load bin/pthreadDetectPass.so
CLANG_LD_FLAGS= -lclangAnalysis -lclangApplyReplacements -lclangARCMigrate -lclangAST	 \
								-lclangASTMatchers -lclangBasic -lclangChangeNamespace -lclangCodeGen  \
								-lclang-cpp -lclangCrossTU -lclangDaemon -lclangDaemonTweaks 					 \
								-lclangDependencyScanning -lclangDirectoryWatcher -lclangDoc					 \
								-lclangDriver -lclangDynamicASTMatchers -lclangEdit -lclangFormat			 \
								-lclangFrontend -lclangFrontendTool -lclangHandleCXX -lclangHandleLLVM \
								-lclangIncludeFixer -lclangIncludeFixerPlugin -lclangIndex -lclangLex	 \
								-lclangMove -lclangParse -lclangQuery -lclangReorderFields						 \
								-lclangRewrite -lclangRewriteFrontend -lclangSema -lclangSerialization \
								-lclang -lclangStaticAnalyzerCheckers -lclangStaticAnalyzerCore				 \
								-lclangStaticAnalyzerFrontend -lclangTooling -lclangToolingASTDiff 		 \
								-lclangToolingCore -lclangToolingInclusions -lclangToolingRefactoring  \
								-lclangToolingSyntax -lclangTransformer

.PHONY: all clean run-pass target

commentParser: commentParser.cpp
	$(CXX) -std=c++14 -c $< $(PASS_CXX_FLAGS)
	$(CXX) $@.o -o identifier $(CLANG_LD_FLAGS)

pthreadDetectPass: pthreadDetectPass.cpp
	$(CC) -fPIC -std=c++14 -c pthreadDetectPass.cpp -o bin/$@.o $(PASS_CXX_FLAGS)
	$(CC) -shared -o bin/$@.so bin/$@.o

target:
	$(CC) -S -std=c++14 -emit-llvm target.cpp -o bin/@.ll

run-pass: pthreadDetectPass target
	opt $(LOAD_FILES) $(OPT_PASS_FLAGS) -S < bin/target.ll > bin/target_optimized.ll

# The third argument of pthread_create: void *(*start_routine) (void *) has chances
# defined in other compilation unit. In that case, we have to make sure start_routine
# is able to access the global duration variable.
test-external-link: run-pass measure.cpp
	$(CXX) -c bin/target_optimized.ll -o bin/target_optimized.o -v
	$(CC) -shared -o bin/libmeasure.so -fPIC measure.cpp
	$(CXX) bin/target_optimized.o $(BUILD_LD_FLAGS) -o bin/optimized
	./bin/optimized

clean:
	/bin/rm -rf bin/*
