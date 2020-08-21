#!/usr/local/bin/python3
import sys
import yaml
import os
import itertools
import logging
import subprocess
import pathlib
from flask import Flask

class NaiveSubject:
    def __init__(self, config_path, verbose=logging.INFO, insertion=True, fix=False):
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
        # Generate content for dylinx-runtime-init.c
        with open(f"{self.out_dir}/dylinx-insertion.yaml", "r") as yaml_file:
            meta = list(yaml.load_all(yaml_file, Loader=yaml.FullLoader))[0]
            init_cu = set()
            for m in meta["LockEntity"]:
                if "extra_init" in m.keys():
                    init_cu.add(m["extra_init"])
            with open(f"{self.home_path}/src/glue/runtime/dylinx-runtime-init.c", "w") as rt_code:
                code = "#include \"../dylinx-glue.h\"\n"
                for cu in init_cu:
                    code = code + f"extern void __dylinx_cu_init_{cu}_();\n"
                code = code + "void __dylinx_global_mtx_init_() {\n"
                for cu in init_cu:
                    code = code + "__dylinx_cu_init_{}_();\n".format(cu)
                code = code + "}\n"
                rt_code.write(code)
        os.chdir(f"{self.home_path}/src/glue/runtime")
        cmd = f"""
            clang -fPIC -c dylinx-runtime-init.c -o {self.home_path}/build/lib/dylinx-runtime-init.o;
            cd {self.home_path}/build/lib;
            clang -shared -o libdlx-init.so dylinx-runtime-init.o;
            /bin/rm -f dylinx-runtime-init.o
        """
        with subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True) as proc:
            logging.debug(proc.stdout.read().decode("utf-8"))

    def inject_symbol(self):
        with subprocess.Popen(args=[self.executable, self.cc_path, f"{self.out_dir}/dylinx-insertion.yaml"], stdout=subprocess.PIPE) as proc:
            out = proc.stdout.read().decode("utf-8")
            logging.debug(out)

    def step(self, include_posix=True):

        with open(f"{self.out_dir}/dylinx-insertion.yaml", "r") as stream:
            meta = list(yaml.load_all(stream, Loader=yaml.FullLoader))
        permutation = []
        for m in meta[0]["LockEntity"]:
            init = ["PTHREADMTX"] if include_posix else []
            if "lock_combination" in m.keys():
                permutation.append(set([*init, *m["lock_combination"]]))

        header_start = (
            "#ifndef __DYLINX_ITERATE_LOCK_COMB__\n"
            "#define __DYLINX_ITERATE_LOCK_COMB__\n"
            "void __dylinx_global_mtx_init_();\n"
        )
        header_end = "\n#endif // __DYLINX_ITERATE_LOCK_COMB__"

        for comb in itertools.product(*permutation):
            current_iter = []
            for i, c in enumerate(comb):
                current_iter.append(f"#define DYLINX_LOCK_TYPE_{i} dylinx_{c.lower()}lock_t")
                current_iter.append(f"#define DYLINX_LOCK_INIT_{i} DYLINX_{c}_INITIALIZER")
            with open(f"{self.home_path}/src/glue/runtime/dylinx-runtime-config.h", "w") as rt_config:
                content = '\n'.join(current_iter)
                rt_config.write(f"{header_start}\n{content}\n{header_end}")
            os.chdir(str(pathlib.PurePath(self.cc_path).parent))
            os.environ["C_INCLUDE_PATH"] = self.home_path
            os.environ["LD_LIBRARY_PATH"] = f"{self.home_path}/build/lib"
            with subprocess.Popen(args=self.build_inst.split(' '), stdout=subprocess.PIPE) as proc:
                logging.debug(proc.stdout.read().decode("utf-8"))
            self.execute()
            yield comb

    def execute(self):
        with subprocess.Popen(args=self.execu_inst.split(' '), stdout=subprocess.PIPE) as proc:
            logging.debug(proc.stdout.read().decode("utf-8"))


app = Flask(__name__)
class InteractiveSubject(NaiveSubject):

    def __init__(self, config_path):
        super().__init__(config_path)
        self.port = conf["port"]
        self.host = conf["host"]

    def run_server(config_path):
        app.run(host='0.0.0.0', port=5566)

def run_client(config_path):
    pass

@app.route("/initialization", methods=["GET"])
def server_init():
    pass
