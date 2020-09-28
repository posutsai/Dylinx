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

def init_wo_callexpr(ltype, entity, content):
    content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_{ltype.lower()}_t")
    if entity.get("define_init", False):
        content.append(
            f"#define DYLINX_LOCK_INIT_{entity['id']} "
            f"{{ malloc(sizeof(dlx_{ltype.lower()}_t)), 0x32CB00B5, {entity['id']}, 0, malloc(sizeof(dlx_injected_interface_t)), {{0}} }}"
        )

def replace_type(ltype, entity, content):
    content.append(f"#define DYLINX_LOCK_TYPE_{entity['id']} dlx_{ltype.lower()}_t")

class NaiveSubject:
    def __init__(self, config_path, verbose=logging.DEBUG, insertion=True, fix=False, include_posix=True):
        self.init_mapping = {
            "VARIABLE": init_wo_callexpr,
            "ARRAY": replace_type,
            "FIELD_INSERT": replace_type,
            "MUTEX_MEM_ALLOCATION": replace_type,
        }
        self.logger = logging.getLogger("dylinx_logger")
        self.logger.setLevel(verbose)
        with open(config_path, "r") as yaml_file:
            conf = list(yaml.load_all(yaml_file, Loader=yaml.FullLoader))[0]
            self.cc_path = os.path.expandvars(conf["compile_commands"])
            self.out_dir = os.path.expandvars(conf["output_directory"])
            self.build_inst = conf["instructions"][0]["build"]
            self.clean_inst = conf["instructions"][1]["clean"]
            self.execu_inst = conf["instructions"][2]["execute"]
        self.home_path = os.environ["DYLINX_HOME"]
        self.executable = f"{self.home_path}/build/bin/dylinx"
        self.inject_symbol()
        with open(f"{self.out_dir}/dylinx-insertion.yaml", "r") as stream:
            meta = list(yaml.load_all(stream, Loader=yaml.FullLoader))[0]
        self.permutation = []
        injection = list(self.filter_out_extern(meta["LockEntity"]))
        for m in injection:
            init = [("PTHREADMTX", m["id"])] if include_posix else []
            comb = set([*init, *m.get("lock_combination", [])])
            if len(comb) == 0:
                raise ValueError("lock_combination shouldn't be empty")
            self.permutation.append(comb)

        self.permutation = list(itertools.product(*self.permutation))
        print(self.permutation)

        # Generate content for dylinx-runtime-init.c
        init_cu = set()
        for m in meta["LockEntity"]:
            if "extra_init" in m.keys():
                init_cu.add(m["extra_init"])
        self.entities = meta["LockEntity"]
        with open(f"{self.home_path}/src/glue/runtime/dylinx-runtime-init.c", "w") as rt_code:
            code = "#include \"../dylinx-glue.h\"\n"
            code = code + "extern void retrieve_native_symbol();\n"
            for cu in init_cu:
                code = code + f"extern void __dylinx_cu_init_{cu}_();\n"
            code = code + "void __dylinx_global_mtx_init_() {\n"
            code = code + "\tretrieve_native_symbol();\n"
            for cu in init_cu:
                code = code + "\t__dylinx_cu_init_{}_();\n".format(cu)
            code = code + "}\n"
            rt_code.write(code)
        cmd = f"""
            clang -fPIC -c {self.home_path}/src/glue/runtime/dylinx-runtime-init.c -o {self.home_path}/build/lib/dylinx-runtime-init.o;
            cd {self.home_path}/build/lib;
            clang -shared -o libdlx-init.so dylinx-runtime-init.o;
            /bin/rm -f dylinx-runtime-init.o
        """
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True) as proc:
            logging.debug(proc.stdout.read().decode("utf-8"))

    def filter_out_extern(self, entities):
        instance = list(filter(lambda e: e["modification_type"] != "EXTERN_VAR_SYMBOL" and e["modification_type"] != "EXTERN_ARR_SYMBOL", entities))
        self.extern_mapping = {}
        for i in instance:
            self.extern_mapping[i["id"]] = []
            for e in entities:
                if e["modification_type"] == "EXTERN_VAR_SYMBOL" or e["modification_type"] == "EXTERN_ARR_SYMBOL":
                    self.extern_mapping[i].append(e)
        return instance

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
        header_end = "\n#endif // __DYLINX_ITERATE_LOCK_COMB__"

        comb = self.permutation[n_comb]
        macro_defs = []
        for c in comb:
            print(c[0])
            self.init_mapping.get(
                self.entities[c[1]]["modification_type"], lambda t, e, content: None
            )(c[0], self.entities[c[1]], macro_defs)
        with open(f"{self.home_path}/src/glue/runtime/dylinx-runtime-config.h", "w") as rt_config:
            content = '\n'.join(macro_defs)
            rt_config.write(f"{header_start}\n{content}\n{header_end}")
        sys.exit()
        os.chdir(str(pathlib.PurePath(self.cc_path).parent))
        os.environ["C_INCLUDE_PATH"] = self.home_path
        os.environ["LD_LIBRARY_PATH"] = f"{self.home_path}/build/lib"
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

