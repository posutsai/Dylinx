add_subdirs("target")
target("dylinx")
  set_kind("binary")
  add_files("src/ast/commentparser.cpp")
  set_targetdir("build/bin")
  set_languages("cxx17", "c99")
  set_toolset("cxx", "/usr/local/bin/clang++")
  set_toolset("ld", "/usr/local/bin/clang++")
  add_cxflags("-fno-rtti", "-fPIC")
  add_includedirs("/usr/local/lib/clang/10.0.0/include")
  add_defines("__DYLINX_DEBUG__")
  add_ldflags(
    "-lclangAnalysis", "-lclangARCMigrate", "-lclangAST",
    "-lclangASTMatchers", "-lclangBasic", "-lclangChangeNamespace",
	"-lclangCodeGen", "-lclang-cpp", "-lclangCrossTU",
    "-lclangDependencyScanning", "-lclangDirectoryWatcher", "-lclangDoc",
    "-lclangDriver", "-lclangDynamicASTMatchers", "-lclangEdit", "-lclangFormat",
    "-lclangFrontend", "-lclangFrontendTool", "-lclangHandleCXX",
	"-lclangHandleLLVM", "-lclangIndex", "-lclangLex",
    "-lclangMove", "-lclangParse", "-lclangQuery", "-lclangReorderFields",
    "-lclangRewrite", "-lclangRewriteFrontend", "-lclangSema", "-lclangSerialization",
    "-lclang", "-lclangStaticAnalyzerCheckers", "-lclangStaticAnalyzerCore",
    "-lclangStaticAnalyzerFrontend", "-lclangTooling", "-lclangToolingASTDiff",
    "-lclangToolingCore", "-lclangToolingInclusions", "-lclangToolingRefactoring",
    "-lclangToolingSyntax", "-lclangTransformer", "-lyaml-cpp",
	"$(shell llvm-config --ldflags --libs --system-libs)"
  )
target_end()

target("dlx-glue")
  set_kind("shared")
  add_files("src/glue/*.c|dylinx-init.c")
  add_includedirs("src/glue")
  add_defines("__DYLINX_DEBUG__")
  set_targetdir("build/lib")
  set_languages("c11")
  set_toolset("cc", "/usr/local/bin/clang")
  add_cflags("-I/usr/local/lib/clang/10.0.0/include")
  add_ldflags("-lpthread")
target_end()

