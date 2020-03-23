add_subdirs("target")

target("criticalSectionMeasure")
  set_default(true)
  set_kind("shared")
  add_files("src/pass/*.cpp")
  set_targetdir("build/lib")
  set_languages("cxx14", "c99")
  set_toolchain("cxx", "/usr/local/bin/clang")
target_end()

target("insertion-point-inspect")
  set_default(true)
  set_kind("binary")
  add_files("src/ast/*.cpp")
  set_targetdir("build/bin")
  set_languages("cxx14", "c99")
  set_toolchain("cxx", "/usr/local/bin/clang")
  add_cxflags("-fno-rtti")
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
    "-lclangToolingSyntax", "-lclangTransformer", "$(shell llvm-config --ldflags --libs --system-libs)"
  )

  after_build(function (target)
    print("[TODO] Build soft link !!!!")
  end)
target_end()

target("comment-parser")
  set_default(true)
  set_kind("binary")
  add_files("src/ast/commentparser.cpp")
  set_targetdir("build/bin")
  set_languages("cxx14", "c99")
  set_toolchain("cxx", "/usr/local/bin/clang")
  add_cxflags("-fno-rtti")
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
    "-lclangToolingSyntax", "-lclangTransformer", "$(shell llvm-config --ldflags --libs --system-libs)"
  )

  after_build(function (target)
    print("[TODO] Build soft link !!!!")
  end)
target_end()

