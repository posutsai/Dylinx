import("core.tool.compiler")
import("core.project.project")
import("core.language.language")

function _make_object(jsonfile, target, sourcefile, objectfile)
  local sourcekind = language.sourcekind_of(sourcefile)

  if sourcekind == "obj" or sourcekind == "lib" then 
    return
  end

  local arguments = table.join(compiler.compargv(sourcefile, objectfile, {target = target, sourcekind = sourcekind}))
  local arguments_escape = {}

  for _, arg in ipairs(arguments) do
    table.insert(arguments_escape, (arg:gsub("[\"\\]", "\\%1")))
  end

  jsonfile:printf(
  [[%s{
    "directory": "%s",
    "arguments": ["%s"],
    "file": "%s"
  }]], ifelse(_g.firstline, "", ",\n"), os.args(os.projectdir()), table.concat(arguments_escape, "\", \""), os.args(sourcefile))

  _g.firstline = false
end

function _make_objects(jsonfile, target, sourcekind, sourcebatch)
  for index, objectfile in ipairs(sourcebatch.objectfiles) do
    _make_object(jsonfile, target, sourcebatch.sourcefiles[index], objectfile)
  end
end

function _make_target(jsonfile, target)

  target:set("pcheader", nil)
  target:set("pcxxheader", nil)

  for _, sourcebatch in pairs(target:sourcebatches()) do
    local sourcekind = sourcebatch.sourcekind
    if sourcekind then
      _make_objects(jsonfile, target, sourcekind, sourcebatch)
    end
  end
end

function make(outputdir, target)

    local oldir = os.cd(os.projectdir())
    local jsonfile = io.open(path.join(outputdir, "compile_commands.json"), "w")

    jsonfile:print("[")
    _g.firstline = true
    _make_target(jsonfile, target)
    jsonfile:print("\n]")
    jsonfile:close()
    os.cd(oldir)
end
