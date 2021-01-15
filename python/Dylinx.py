#!/usr/local/bin/python3
import sys
import yaml
import os
import itertools
import logging
import subprocess
import pathlib
import shutil
import abc
import ctypes
import glob
import re
import ctypes
from enum import Enum
import matplotlib.pyplot as plt
import numpy as np

# ALLOWED_LOCK_TYPE = ["PTHREADMTX", "ADAPTIVEMTX", "TTAS", "BACKOFF", "MCS"]
ALLOWED_LOCK_TYPE = ["PTHREADMTX", "ADAPTIVEMTX", "TTAS", "BACKOFF"]

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

class BaseSubject(metaclass=abc.ABCMeta):
    def __init__(self, cc_path, out_dir=None, verbose=logging.DEBUG):
        # Initializing class required attribute and environment variable
        self.logger = logging.getLogger("dylinx_logger")
        self.logger.setLevel(verbose)
        self.cc_path = cc_path
        self.glue_dir = os.path.abspath(os.path.dirname(self.cc_path) + "/.dylinx")
        if out_dir == None:
            self.out_dir = os.getcwd()
        else:
            self.out_dir = out_dir
        self.home_path = os.environ["DYLINX_HOME"]
        self.executable = f"{self.home_path}/build/bin/dylinx"
        with subprocess.Popen(args=['clang', '-dumpversion'], stdout=subprocess.PIPE, universal_newlines=True) as proc:
            clang_ver = proc.stdout.read().replace('\n', '')
        os.environ["C_INCLUDE_PATH"] = ":".join([f"{self.home_path}/src/glue", f"/usr/local/lib/clang/{clang_ver}/include", f"{self.glue_dir}/glue"])

        self.init_mapping = {
            "VARIABLE": var_macro_handler,
            "ARRAY": arr_macro_handler,
            "FIELD_INSERT": field_decl_handler,
            "MUTEX_MEM_ALLOCATION": mtx_alloc_handler,
            "EXTERN_VAR_SYMBOL": extern_symbol_handler,
            "EXTERN_ARR_SYMBOL": extern_symbol_handler,
            "STRUCT_MEM_ALLOCATION": struct_alloc_handler
        }
        self.inject_symbol()
        with open(f"{self.out_dir}/dylinx-insertion.yaml", "r") as stream:
            meta = list(yaml.load_all(stream, Loader=yaml.FullLoader))[0]
        self.altered_files = meta["AlteredFiles"]
        self.permutation = []
        slots = list(self.filter_valid(meta["LockEntity"]))
        self.pluggable_sites = {}
        for m in slots:
            self.pluggable_sites[m['id']] = m["lock_combination"] if m.get("lock_combination", None) else []

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

    def get_pluggable(self):
        return self.pluggable_sites

    def configure_type(self, id2type):
        with subprocess.Popen("clang -dumpversion", stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True) as proc:
            clang_version = proc.stdout.read().decode("utf-8")
            proc.stderr.read().decode("utf-8")
        id2type = {int(k): v for k,v in id2type.items()}
        header_start = (
            "#ifndef __DYLINX_ITERATE_LOCK_COMB__\n"
            "#define __DYLINX_ITERATE_LOCK_COMB__\n"
            "void __dylinx_global_mtx_init_();\n"
        )
        header_end = "\n#endif // __DYLINX_ITERATE_LOCK_COMB__\n"
        macro_defs = []
        for cu_id in self.extra_init_cu:
            macro_defs.append(f"void __dylinx_cu_init_{cu_id}_(void);")
        for i, e in self.entities.items():
            self.init_mapping.get(e["modification_type"], lambda i, i2t, e, m: None)(i, id2type, self.entities, macro_defs)
        with open(f"{self.glue_dir}/glue/dylinx-runtime-config.h", "w") as rt_config:
            content = '\n'.join(macro_defs)
            rt_config.write(f"{header_start}\n{content}\n{header_end}")
        # os.chdir(str(pathlib.PurePath(self.cc_path).parent))
        os.environ["C_INCLUDE_PATH"] = ":".join([f"{self.home_path}/src/glue", f"/usr/local/lib/clang/{clang_version}/include", f"{self.glue_dir}/glue"])
        os.environ["LIBRARY_PATH"] = ":".join([f"{self.home_path}/build/lib", f"{self.glue_dir}/lib"])

    def revert_repo(self):
        src2path = { pathlib.Path(f).name: f  for f in self.altered_files }
        for f in glob.glob(f"{self.glue_dir}/src/*"):
            name = pathlib.Path(f).name
            shutil.copyfile(f, src2path[name])
        shutil.rmtree(self.glue_dir)
        print("Target is reverted !!!")

    @abc.abstractmethod
    def build_repo(self):
        raise NotImplementedError

    @abc.abstractmethod
    def execute_repo(self):
        raise NotImplementedError

    @abc.abstractmethod
    def stop_repo(self):
        raise NotImplementedError

class OpKind(Enum):
    ENABLE = 1
    DISABLE = 0

class _HeadTrace:
    def __init__(self, groups):
        assert(groups[0] in ["enable", "disable"])
        func, arg, cpu, tid, pid, self.kind, tsc = groups
        self.affined_cpu = int(cpu)
        self.tid = int(tid)
        self.pid = int(pid)
        self.tsc = int(tsc)
        assert(self.kind == "enter-arg")
        self.site_id, self.ins_id = self.parse_lock_id(int(arg))
        self.op = {"enable": OpKind.ENABLE, "disable": OpKind.DISABLE}[func]

    def parse_lock_id(self, long_id):
        ins_id = ctypes.c_int((long_id & 0xFFFFFFFF00000000) >> 32).value
        site_id = ctypes.c_int(long_id & 0x00000000FFFFFFFF).value
        return site_id, ins_id

class _TailTrace:
    def __init__(self, groups):
        assert(groups[0] in ["enable", "disable"])
        func, _, cpu, tid, pid, self.kind, tsc = groups
        self.affined_cpu = int(cpu)
        self.tid = int(tid)
        self.pid = int(pid)
        self.tsc = int(tsc)
        self.op = {"enable": OpKind.ENABLE, "disable": OpKind.DISABLE}[func]
        assert(self.kind == "exit")

class _LockOp:
    def __init__(self, head_trace, tail_trace):
        assert(isinstance(head_trace, _HeadTrace) == True)
        assert(isinstance(tail_trace, _TailTrace) == True)
        assert(head_trace.tid == tail_trace.tid)
        assert(head_trace.pid == tail_trace.pid)
        self.op = head_trace.op
        self.affined_cpu = head_trace.affined_cpu
        self.tid = head_trace.tid
        self.pid = head_trace.pid
        self.enter_tsc = head_trace.tsc
        self.exit_tsc = tail_trace.tsc
        self.site_id = head_trace.site_id
        self.ins_id = head_trace.ins_id
        self.duration = self.exit_tsc - self.enter_tsc

class DylinxLockCycle:
    def __init__(self, enable, disable):
        assert(enable.site_id == disable.site_id)
        assert(enable.ins_id == disable.ins_id)
        self.site_id = enable.site_id
        self.ins_id = enable.ins_id
        self.affined_cpu = enable.affined_cpu
        self.tid = enable.tid
        self.pid = enable.pid
        self.rel_wait = enable.exit_tsc - enable.enter_tsc
        self.abs_wait = (enable.enter_tsc, enable.exit_tsc)
        self.rel_hold = disable.enter_tsc - enable.exit_tsc
        self.abs_hold = (enable.exit_tsc, disable.enter_tsc)
        self.rel_life = disable.enter_tsc - enable.enter_tsc
        self.abs_life = (enable.enter_tsc, disable.enter_tsc)
        self.attempt = enable.enter_tsc
        self.acquire = enable.exit_tsc
        self.release = disable.enter_tsc

    def serialize(self):
        pass

class DylinxRuntimeReport:
    def __init__(self, xray_path):
        pattern = re.compile(r"- { type: 0, func-id: \d+, function: dlx_forward_(enable|disable),(?: args: \[ (\d+) \],)? cpu: (\d*), thread: (\d*). process: (\d*), kind: function-(enter\-arg|exit), tsc: (\d*), data: '' }")
        with open(xray_path) as xray_file:
            xray_log = xray_file.read()
            entries = re.findall(pattern, xray_log)
            traces = list(map(lambda groups: _TailTrace(groups) if groups[1] == "" else _HeadTrace(groups), entries))
        pid = {t.pid for t in traces}
        assert(len(pid) == 1) # xray log should contain only single main thread.
        main_tid = list(pid)[0]
        threads = {t.tid for t in traces}
        self.cycles = []
        for tid in threads:
            traces_by_tid = list(filter(lambda trace: trace.tid == tid, traces))
            self.cycles = self.cycles + self.pair_thread_traces(traces_by_tid)
        print("cycle processing complete")

        self.site2ins = {}
        for trace in filter(lambda t: isinstance(t, _HeadTrace), traces):
            site_id = trace.site_id
            ins_id = trace.ins_id
            if site_id not in self.site2ins.keys():
                self.site2ins[site_id] = {ins_id}
            else:
                self.site2ins[site_id].add(ins_id)
        print(self.site2ins)


    def pair_thread_traces(self, traces):
        sorted_traces = sorted(traces, key=lambda t: t.tsc)
        assert(len(traces) % 2 == 0)
        cycles = []
        stack = []
        for i_pair in range(len(sorted_traces) // 2):
            curr_call = _LockOp(sorted_traces[i_pair * 2], sorted_traces[i_pair * 2 + 1])
            if curr_call.op == OpKind.ENABLE:
                stack.append(curr_call)
            if curr_call.op == OpKind.DISABLE:
                prev_call = stack.pop()
                if prev_call.op != OpKind.ENABLE or (curr_call.site_id, curr_call.ins_id) != (prev_call.site_id, prev_call.ins_id):
                    raise ValueError(f"[ ERROR ] Disable and enable op pair aren't matched")
                cycles.append(DylinxLockCycle(prev_call, curr_call))
        return cycles

    # Should we plot from main function
    def plot_lifetime_superposition_graph(self, fig_path):
        fig, axes = plt.subplots(len(self.site2ins.keys()), 1, sharex=True)
        fig.tight_layout(pad=3.0)
        # Current version starts from the first lock related op.
        s_tick = min(self.cycles, key=lambda c: c.attempt).attempt
        e_tick = max(self.cycles, key=lambda c: c.release).release
        timeline_len = e_tick - s_tick + 1
        x = np.arange(timeline_len)
        for i, site in enumerate(self.site2ins.keys()):
            y = np.zeros(timeline_len)
            involve_cycles = filter(lambda c: c.site_id == site, self.cycles)
            for cycle in involve_cycles:
                y[cycle.attempt - s_tick: cycle.release - s_tick] += 1
            axes[i].set_title(f"lifetime superposition for site {site}")
            axes[i].plot(x, y)
        plt.savefig(fig_path)

