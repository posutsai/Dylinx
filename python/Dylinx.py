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
import matplotlib as mpl
import numpy as np
import pickle

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
        if (e["modification_type"] == "FIELD_INSERT" or e["modification_type"] == "FIELD_ARRAY") and e["field_name"] == name and e["fentry_uid"] == f_uid and e["line"] == line:
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
            "FIELD_ARRAY": field_decl_handler,
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
                e["modification_type"] != "VAR_FIELD_INIT" and \
                e["modification_type"] != "STRUCT_MEM_ALLOCATION"

        validity = list(filter(valid, entities))
        return validity

    def inject_symbol(self):
        cmd = f"{self.executable} {self.cc_path} {self.out_dir}/dylinx-insertion.yaml"
        with subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True) as proc:
            out = proc.stdout.read().decode("utf-8")
            err = proc.stderr.read().decode("utf-8")
            logging.debug(out)
            logging.debug(err)

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
    def __str__(self):
        return f"HeadTrace {self.tsc}"

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
    def __str__(self):
        return f"TailTrace {self.tsc}"

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

    def __str__(self):
        return f"(site, ins) = {self.site_id, self.ins_id}, op: {self.op}, tid: {self.tid}, enter_ts: {self.enter_tsc}, exit_ts: {self.exit_tsc}"

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

def smooth(y, box_pts):
    box = np.ones(box_pts)/box_pts
    y_smooth = np.convolve(y, box, mode='same')
    return y_smooth

def moving_average(a, n):
    ret = np.cumsum(a, dtype=float)
    ret[n:] = ret[n:] - ret[:-n]
    return ret / n

class SiteFluctuation:
    def __init__(self, s_tick, e_tick, cycles, site_id, stable_threshold=0.005, window_ratio=0.01, get_stable=False):
        self.s_tick = s_tick
        self.e_tick = e_tick
        self.stable_threshold = stable_threshold
        self.window_ratio = window_ratio
        self.get_stable = get_stable
        self.involve_cycles = filter(lambda c: c.site_id == site_id, cycles)
        timeline_len = self.e_tick - self.s_tick + 1
        y = np.zeros(timeline_len)
        for cycle in self.involve_cycles:
            y[cycle.attempt - self.s_tick: cycle.release - self.s_tick] += 1
        self.y_raw = np.copy(y)
        if not get_stable:
            return
        self.y_smooth = moving_average(y, int(y.shape[0] * window_ratio))
        self.y_diff = np.gradient(self.y_smooth, 0.00001)
        self.stable_region = (self.y_diff > -stable_threshold) & (self.y_diff < stable_threshold)
        n_stable_tick = np.count_nonzero(self.stable_region & (self.y_smooth != 0))
        total_tick = np.count_nonzero(y_smooth)
        self.stable_ratio = n_stable_tick / total_tick

def pair_first(operations):
    front = operations[0]
    if front.op != OpKind.ENABLE:
        return front, None
    for left in operations[1:]:
        if left.op == OpKind.DISABLE and left.site_id == front.site_id and left.ins_id == front.ins_id:
            return front, left
    return front, None

class DylinxRuntimeReport:
    def __init__(self, xray_path, drop_useless=False):
        pattern = re.compile(r"- { type: 0, func-id: \d+, function: dlx_forward_(enable|disable),(?: args: \[ (\d+) \],)? cpu: (\d*), thread: (\d*). process: (\d*), kind: function-(enter\-arg|exit), tsc: (\d*), data: '' }")
        with open(xray_path) as xray_file:
            xray_log = xray_file.read()
            entries = re.findall(pattern, xray_log)
            traces = list(map(lambda groups: _TailTrace(groups) if groups[-2] == "exit" else _HeadTrace(groups), entries))
        pid = {t.pid for t in traces}
        assert(len(pid) == 1) # xray log should contain only single main thread.
        main_tid = list(pid)[0]
        threads = {t.tid for t in traces}
        self.cycles = []
        for tid in threads:
            traces_by_tid = list(filter(lambda trace: trace.tid == tid, traces))
            self.cycles = self.cycles + self.pair_thread_traces(traces_by_tid, drop_useless)

        self.site2ins = {}
        for trace in filter(lambda t: isinstance(t, _HeadTrace), traces):
            site_id = trace.site_id
            ins_id = trace.ins_id
            if site_id not in self.site2ins.keys():
                self.site2ins[site_id] = {ins_id}
            else:
                self.site2ins[site_id].add(ins_id)

        self.s_tick = min(self.cycles, key=lambda c: c.attempt).attempt
        self.e_tick = max(self.cycles, key=lambda c: c.release).release
        self.timeline_len = self.e_tick - self.s_tick + 1
        self.site2fluc = None

    def pair_thread_traces(self, traces, drop_useless):
        sorted_traces = sorted(traces, key=lambda t: t.tsc)
        if not drop_useless:
            assert(len(traces) % 2 == 0)

        ops = []
        first_trace =  next((t for t in sorted_traces if isinstance(t, _HeadTrace)), None)
        offset = sorted_traces.index(first_trace)
        for i_pair in range((len(sorted_traces) - offset) // 2):
            head = sorted_traces[i_pair * 2 + offset]
            tail = sorted_traces[i_pair * 2 + 1 + offset]
            assert(head.op == tail.op)
            curr_call = _LockOp(head, tail)
            ops.append(curr_call)

        cycles = []
        front, paired = pair_first(ops)
        if paired != None:
            cycles.append(DylinxLockCycle(front, paired))

        while (paired != None or len(ops) > 2):
            ops.remove(front)
            if paired != None:
                ops.remove(paired)
            if len(ops) < 2:
                break
            front, paired = pair_first(ops)
            if paired != None:
                cycles.append(DylinxLockCycle(front, paired))
        print(len(cycles), "n_cycles")
        return cycles

    def gen_bool_fluc(self, unit=1000000, top_n=10):
        print(f"x axis len {self.timeline_len // unit + 10}")
        site2fluc = {site_id: np.zeros(self.timeline_len // unit + 10) for site_id in self.site2ins.keys()}
        for cycle in self.cycles:
            start_ts = (cycle.attempt - self.s_tick) // unit
            end_ts = (cycle.release - self.s_tick) // unit
            site2fluc[cycle.site_id][start_ts: end_ts + 1] = 1
        site_ids = site2fluc.keys()
        site_ids = sorted(site_ids, key=lambda s: -np.count_nonzero(site2fluc[s]))
        for site, fluc in site2fluc.items():
            print(f"[site {site}]: non-zero {np.count_nonzero(fluc)} {np.count_nonzero(fluc) / (self.timeline_len // unit + 10)}")
        cmap = mpl.colors.ListedColormap(['antiquewhite', 'k'])
        bounds = [0., 0.5, 1.]
        norm = mpl.colors.BoundaryNorm(bounds, cmap.N)
        fig, axes = plt.subplots(top_n, 1, sharex=True, constrained_layout=True)
        fig.set_size_inches(12, top_n * 1)
        for i, site_id in enumerate(site_ids[:top_n]):
            fluc = site2fluc[site_id]
            x = np.array([fluc, ]* 300)
            ax = axes if len(site2fluc.keys()) == 1 else axes[i]
            ax.imshow(x, interpolation='none', cmap=cmap, norm=norm)
            ax.get_yaxis().set_visible(False)
            ax.set_title(
                f"Active period, site[{site_id}]"
            )
        ax.set_xlabel(f"Timeline ({unit} cycles)")
        plt.savefig("test.png")
        return site2fluc

    def q_entity_curve_eval(self, stable_threshold=0.005, window_ratio=0.01, get_stable=False, sieve=None):
        site2stability = {}
        iter_keys = sieve(self.site2ins.keys()) if sieve != None else self.site2ins.keys()
        self.site2fluc  = {
            site_id: SiteFluctuation(self.s_tick, self.e_tick, self.cycles, site_id)
            for site_id in iter_keys
        }

    # Should we plot from main function
    def plot_lifetime_superposition_graph(self, fig_path):
        if self.site2fluc == None:
            raise ValueError("q_entity_curve_eval method should be called first before ploting")
        fig, axes = plt.subplots(len(self.site2fluc.keys()), 1, sharex=True, constrained_layout=True)
        fig.set_size_inches(12, len(self.site2fluc.keys()) * 3)
        # Current version starts from the first lock related op.
        x = np.arange(self.timeline_len)
        for i, pair in enumerate(self.site2fluc.items()):
            site_id, fluc = pair
            ax = axes if len(self.site2fluc.keys()) == 1 else axes[i]
            ax.plot(x, fluc.y_raw)
            if fluc.get_stable:
                ax.plot(x, self.site2fluc[site_id].y_smooth, color="orange", alpha=0.5)
                ax.fill_between(
                    x, site2fluc[site_id].y_smooth,
                    where=site2fluc[site_id].stable_region, color="orange", alpha=0.5
                )
            ax.set_title(
                f"Number of thread in queueing system, site[{site_id}]"
            )
        ax.set_xlabel("Timeline (cycle)")
        plt.savefig(fig_path)

