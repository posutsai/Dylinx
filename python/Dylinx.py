#!/usr/local/bin/python3
import sys
import yaml
import os
import itertools
import logging
import subprocess
import pathlib
from flask import request, jsonify, Blueprint
import requests

def var_macro_handler(i, id2type, entities, content):
    ltype = id2type[i]
    entity = entities[i]
    content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_{ltype.lower()}_t")
    if entity.get("define_init", False):
        content.append(
            f"#define DYLINX_LOCK_INIT_{entity['id']} "
            f"{{ malloc(sizeof(dlx_{ltype.lower()}_t)), 0x32CB00B5, {{ {entity['id']}, 100 }}, &dlx_{ltype.lower()}_methods_collection, {{0}} }}"
        )
    if entity.get("extra_init", False):
        content.append(
            f"#define DYLINX_VAR_DECL_{entity['fentry_uid']}_{entity['line']} {entity['id']}"
        )

def arr_macro_handler(i, id2type, entities, content):
    ltype = id2type[i]
    entity = entities[i]
    content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_{ltype.lower()}_t")
    content.append(f"#define DYLINX_ARRAY_DECL_{entity['fentry_uid']}_{entity['line']} {entity['id']}")

def mtx_alloc_handler(i, id2type, entities, content):
    ltype = id2type[i]
    entity = entities[i]
    content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_{ltype.lower()}_t")
    content.append(f"#define DYLINX_LOCK_INIT_{entity['id']} NULL")
    content.append(f"#define DYLINX_LOCK_OBJ_INDICATOR_{entity['id']} (int []){{ {entity['id']} }}")

def locate_field_decl(name, f_uid, line, entities):
    for i, e in entities.items():
        if e["modification_type"] == "FIELD_INSERT" and e["field_name"] == name and e["fentry_uid"] == f_uid and e["line"] == line:
            return e

def field_decl_handler(i, id2type, entities, content):
    ltype = id2type[i]
    entity = entities[i]
    content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_{ltype.lower()}_t")
    content.append(f"#define DYLINX_FIELD_DECL_{entity['fentry_uid']}_{entity['line']} {entity['id']}")


def struct_alloc_handler(i, id2type, entities, content):
    entity = entities[i]
    content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} user_def_struct_t")
    init_methods = []
    indicators = []

    for m in entity["member_info"]:
        decl = locate_field_decl(m["field_name"], m["fentry_uid"], m["line"], entities)
        field_ltype = id2type[decl["id"]]
        indicators.append(str(decl["id"]))
        init_methods.append(f"(void *)dlx_{field_ltype.lower()}_var_init")
    id_str = "(int []) { " + ",".join(indicators) + "}"
    init_methods = "(void *[]) { " + ",".join(init_methods) + "}"
    content.append(f"#define DYLINX_LOCK_INIT_{entity['id']} {init_methods}")
    content.append(f"#define DYLINX_LOCK_OBJ_INDICATOR_{entity['id']} {id_str}")



def extern_symbol_handler(i, id2type, entities, content):
    def locate_var_decl(name):
        for i, e in entities.items():
            if (e["modification_type"] == "VARIABLE" or e["modification_type"] == "ARRAY") and e["name"] == name:
                return e
        print(f"[WARNING] valid symbol mapping {name} is not found")
        return None
    entity = entities[i]
    valid = locate_var_decl(entity["name"])
    if valid != None:
        ltype = id2type[valid["id"]]
        content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_{ltype.lower()}_t")
    else:
        content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_pthreadmtx_t")

class NaiveSubject:
    def __init__(self, config_path, verbose=logging.DEBUG, insertion=True, fix=False, include_posix=True):
        self.init_mapping = {
            "VARIABLE": var_macro_handler,
            "ARRAY": arr_macro_handler,
            "FIELD_INSERT": field_decl_handler,
            "MUTEX_MEM_ALLOCATION": mtx_alloc_handler,
            "EXTERN_VAR_SYMBOL": extern_symbol_handler,
            "EXTERN_ARR_SYMBOL": extern_symbol_handler,
            "STRUCT_MEM_ALLOCATION": struct_alloc_handler
        }
        self.logger = logging.getLogger("dylinx_logger")
        self.logger.setLevel(verbose)
        with open(config_path, "r") as yaml_file:
            conf = list(yaml.load_all(yaml_file, Loader=yaml.FullLoader))[0]
            self.cc_path = os.path.expandvars(conf["compile_commands"])
            os.chdir(os.path.dirname(os.path.expandvars(conf["compile_commands"])))
            self.glue_dir = os.path.dirname(self.cc_path) + "/.dylinx"
            self.out_dir = os.path.expandvars(conf["output_directory"])
            self.build_inst = conf["instructions"][0]["build"]
            self.clean_inst = conf["instructions"][1]["clean"]
            self.execu_inst = conf["instructions"][2]["execute"]
        self.home_path = os.environ["DYLINX_HOME"]
        self.executable = f"{self.home_path}/build/bin/dylinx"
        os.environ["C_INCLUDE_PATH"] = ":".join([f"{self.home_path}/src/glue", "/usr/local/lib/clang/10.0.0/include", f"{self.glue_dir}/glue"])
        self.inject_symbol()
        with open(f"{self.out_dir}/dylinx-insertion.yaml", "r") as stream:
            meta = list(yaml.load_all(stream, Loader=yaml.FullLoader))[0]
        self.permutation = []
        validity = list(self.filter_valid(meta["LockEntity"]))
        for m in validity:
            init = [("PTHREADMTX", m["id"])] if include_posix else []
            parsed_comb = m.get("lock_combination", [])
            parsed_comb = [] if parsed_comb is None else parsed_comb
            comb = set([*init, *parsed_comb])
            if len(comb) == 0:
                raise ValueError("lock_combination shouldn't be empty")
            self.permutation.append(comb)

        self.permutation = list(itertools.product(*self.permutation))

        # Generate content for dylinx-runtime-init.c
        init_cu = set()
        for m in meta["LockEntity"]:
            if "extra_init" in m.keys():
                init_cu.add(m["fentry_uid"])
        self.extra_init_cu = init_cu
        self.entities = { e["id"]: e for e in meta["LockEntity"] }
        with open(f"{self.glue_dir}/glue/dylinx-runtime-init.c", "w") as rt_code:
            code = "#include \"dylinx-glue.h\"\n"
            code = code + "extern void retrieve_native_symbol();\n"
            for cu in init_cu:
                code = code + f"extern void __dylinx_cu_init_{cu}_();\n"
            code = code + "void __dylinx_global_mtx_init_() {\n"
            code = code + "\tretrieve_native_symbol();\n"
            code = code + "\tassert(sizeof(dlx_generic_lock_t) == sizeof(pthread_mutex_t));\n"
            for cu in init_cu:
                code = code + "\t__dylinx_cu_init_{}_();\n".format(cu)
            code = code + "}\n"
            rt_code.write(code)
        cmd = f"""
            cd {self.glue_dir}/glue;
            clang -c {self.glue_dir}/glue/dylinx-runtime-init.c -o {self.glue_dir}/lib/dylinx-runtime-init.o -I{self.home_path}/src/glue -I.;
            ar rcs -o {self.glue_dir}/lib/libdlx-init.a {self.glue_dir}/lib/dylinx-runtime-init.o;
            /bin/rm -f {self.glue_dir}/lib/dylinx-runtime-init.o
        """
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True) as proc:
            logging.debug(proc.stdout.read().decode("utf-8"))

    def filter_valid(self, entities):
        def valid(e):
            return \
                e["modification_type"] != "EXTERN_VAR_SYMBOL" and \
                e["modification_type"] != "EXTERN_ARR_SYMBOL" and \
                e["modification_type"] != "STRUCT_MEM_ALLOCATION"

        validity = list(filter(valid, entities))
        return validity

    def inject_symbol(self):
        with subprocess.Popen(args=[self.executable, self.cc_path, f"{self.out_dir}/dylinx-insertion.yaml"], stdout=subprocess.PIPE) as proc:
            out = proc.stdout.read().decode("utf-8")
            logging.debug(out)

    def get_num_perm(self):
        return len(self.permutation)

    def step(self, n_comb):

        header_start = (
            "#ifndef __DYLINX_ITERATE_LOCK_COMB__\n"
            "#define __DYLINX_ITERATE_LOCK_COMB__\n"
            "void __dylinx_global_mtx_init_();\n"
        )
        header_end = "\n#endif // __DYLINX_ITERATE_LOCK_COMB__\n"

        comb = self.permutation[n_comb]
        id2type = { c[1]: c[0] for c in comb }
        macro_defs = []
        for cu_id in self.extra_init_cu:
            macro_defs.append(f"void __dylinx_cu_init_{cu_id}_(void);")
        for i, e in self.entities.items():
            self.init_mapping.get(e["modification_type"], lambda i, i2t, e, m: None)(i, id2type, self.entities, macro_defs)
        with open(f"{self.glue_dir}/glue/dylinx-runtime-config.h", "w") as rt_config:
            content = '\n'.join(macro_defs)
            rt_config.write(f"{header_start}\n{content}\n{header_end}")
        os.chdir(str(pathlib.PurePath(self.cc_path).parent))
        os.environ["C_INCLUDE_PATH"] = ":".join([f"{self.home_path}/src/glue", "/usr/local/lib/clang/10.0.0/include", f"{self.glue_dir}/glue"])
        os.environ["LIBRARY_PATH"] = ":".join([f"{self.home_path}/build/lib", f"{self.glue_dir}/lib"])
        sys.exit()
        with subprocess.Popen(self.build_inst, stdout=subprocess.PIPE, shell=True) as proc:
            logging.debug(proc.stdout.read().decode("utf-8"))
        return comb

    def execute(self):
        with subprocess.Popen(self.execu_inst, stdout=subprocess.PIPE, shell=True) as proc:
            logging.debug(proc.stdout.read().decode("utf-8"))

def blueprint_gen():

    DylinxBlueprint = Blueprint("Dylinx", "Dylinx")

    @DylinxBlueprint.route("/init", methods=["POST"])
    def init():
        global instance
        Cls = getattr(sys.modules[request.json["mod"]], request.json["cls"])
        instance = Cls(request.json["config_path"])
        return jsonify({"error_code": 0})

    @DylinxBlueprint.route("/perm", methods=["GET"])
    def get_permutations():
        global instance
        return jsonify({
            "error_code": 0,
            "permutation": instance.permutation
        })

    @DylinxBlueprint.route("/step/<it>", methods=["GET"])
    def step(it):
        global instance
        instance.step(int(it))
        return jsonify({
            "error_code": 0,
        })
    i
    return DylinxBlueprint

