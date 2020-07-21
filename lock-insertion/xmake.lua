add_subdirs("target")
target("dylinx")
  set_kind("binary")
  add_files("src/ast/commentparser.cpp")
  set_targetdir("build/bin")
  set_languages("cxx17", "c99")
  set_toolchain("cxx", "/usr/local/bin/clang")
  add_cxflags("-fno-rtti")
  add_includedirs("include", "/home/plate/llvm-project/clang/lib")
  add_ldflags(
    "-lclangAnalysis", "-lclangApplyReplacements", "-lclangARCMigrate", "-lclangAST",
    "-lclangASTMatchers", "-lclangBasic", "-lclangChangeNamespace", "-lclangCodeGen",
    "-lclang-cpp", "-lclangCrossTU", "-lclangDaemon", "-lclangDaemonTweaks",
    "-lclangDependencyScanning", "-lclangDirectoryWatcher", "-lclangDoc",
    "-lclangDriver", "-lclangDynamicASTMatchers", "-lclangEdit", "-lclangFormat",
    "-lclangFrontend", "-lclangFrontendTool", "-lclangHandleCXX", "-lclangHandleLLVM",
    "-lclangIncludeFixer", "-lclangIncludeFixerPlugin", "-lclangIndex", "-lclangLex",
    "-lclangMove", "-lclangParse", "-lclangQuery", "-lclangReorderFields",
    "-lclangRewrite", "-lclangRewriteFrontend", "-lclangSema", "-lclangSerialization",
    "-lclang", "-lclangStaticAnalyzerCheckers", "-lclangStaticAnalyzerCore",
    "-lclangStaticAnalyzerFrontend", "-lclangTooling", "-lclangToolingASTDiff",
    "-lclangToolingCore", "-lclangToolingInclusions", "-lclangToolingRefactoring",
    "-lclangToolingSyntax", "-lclangTransformer", "$(shell llvm-config --ldflags --libs --system-libs)",
    "-lyaml-cpp"
  )

  after_build(function (target)
    print("[TODO] Build soft link !!!!")
  end)
target_end()

target("dlx-glue")
  set_kind("shared")
  add_files("src/glue/*.c")
  add_includedirs("src/glue")
  -- add_defines("__DYLINX_DEBUG__")
  set_targetdir("build/lib")
  set_languages("c99")
  set_toolchain("cc", "/usr/local/bin/clang")
  add_cflags("-I/usr/local/lib/clang/10.0.0/include")
  add_ldflags("-lpthread")
target_end()

