# Dylinx

Dylinx stands for "Dynamic Lock Instrumentation and Analysis". Dylinx is an tool that enables programmers to apply **heterogeneous arrangement** on the application with multiple mutexes automatically. Namely, programmers can customize a specific contention handling method (mutex implementation) for certain mutex. Such heterogeneity reduces the performance loss due to mutex contention as much as possible.

## Install Dylinx and Run a subject
### Prerequisite
* Clang with Xray, libASTMatcher and libTooling (Please activate **clang-tools-extra** while building Clang)
* [Bear](https://github.com/rizsotto/Bear)
* [xmake](https://github.com/xmake-io/xmake)
* An application written in pure C.
### Building Dylinx and Static Library
`$ xmake build` generates both an executable named Dylinx and a static library named `dlx-glue`
### Integration with Existing Pipeline
One of Dylinx's advantages is that Dylinx can easily integrate with existing pipeline with following two steps.
1. Set `DYLINX_HOME` environment variable to the path you clone the repo.
2. Generate compilation database of application. After running the following command, a JSON file named `compiler_commands.json` should be generated.

   ```
   $ bear make my-project
   ```
   
4. Add necessary flags in compilation. (`-latomic -lpthread -ldl -ldlx-init -ldlx-glue -fxray-instrument`)
5. Implement a Python class which inherits BaseSubject.
6. Design an optimization strategy for your goal.
Please refer the succeeding section for further detail. It gives an example that integrates Dylinx with [memcached](https://github.com/memcached/memcached).

## Memcached as Example
All concrete codes locates in [memcached directory](sample/memcached).

1. Switch Default Compiler
   ```
   $ CC=clang ./configure
   ```
   Following the official building steps but switch the compiler to `clang`.

2. Generating compiler database.
   ```
   $ bear make memcached
   ```

3. Adding Flags in Compilation
   ```
   $ ln -s dylinx.mk repo 
   ```
   
   ```Makefile
   DYLINX_LIB_WITH_GLUE=-L$(DYLINX_HOME)/build/lib -L.dylinx/lib -latomic -lpthread -ldl -ldlx-init -ldlx-glue -fxray-instrument
   ```
   Please refer to [dylinx.mk](sample/memcached/dylinx.mk). After memcached's Makefile is generated from the previous step, `dylinx.mk` declare another variable `DYLINX_LIB_WITH_GLUE` which allow compiler to locate Dylinx glue library and link it. Note that Dylinx automatically generates a temporary directory `.dylinx` for those required glue files and `libdlx-init.a` locates in it. Make sure your Clang is able to find the library.

4. Implement Child Class
   Please refer to `DylinxSubject` in [server.py](sample/memcached/server.py). Users should implement `build_repo`, `execute_repo` and `stop_repo` methods for their own application. In this example, the executable (`memcached-dlx`) is built, executed and killed by spawning a child process.
   ```Python
   class DylinxSubject:
     def __init__(self, compiler_database_path):
       super().__init__(coompiler_database_path) # .dylinx temporary directory is generated after initialization
       # ...
     def build_repo(self):
       subprocess.Popen("cd repo; make -f dylinx.mk memcached-dlx")
       # ...
     def execute_repo(self):
       subprocess.Popen("./memcached-dlx")
       # ...
     def stop_repo(self):
       # send terminate signal
   ```
   After this step, the integration is done and there will be a `dylinx-insertion.yaml` file in memcached directory. It reveals the identified mutexes and their corresponding location (path, line number, name ....).

5. By default, Dylinx offers 4 mutex implementation in its library. If n mutexes are identified, the search space is 4^n. When the number of mutexes increases, the search space expands exponentially. It is necessary to design your own strategy to explore the oversized search space and obtain the optimal arrangement.

ps. Please refer to wiki for all the details of memcached example.
