#!/usr/local/bin/python3
import sys
import glob
import os
import subprocess
import re
import itertools
import pickle
import numpy as np
if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, DylinxRuntimeReport, ALLOWED_LOCK_TYPE

class ColonySubject(BaseSubject):
    def __init__(self, cc_path):
        self.xray_option = "\"patch_premain=true xray_mode=xray-basic xray_logfile_base=xray-log/ \""
        self.basic_option = "\"func_duration_threshold_us=0\""
        super().__init__(cc_path)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.revert_repo()
        pass

    def build_repo(self, id2type):
        super().configure_type(id2type)
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{os.environ['DYLINX_HOME']}/sample/colony/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        cmd = f"make clean; make colony-inspect with_dlx=1"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self):

        with subprocess.Popen(
            f"XRAY_OPTIONS={self.xray_option} XRAY_BASIC_OPTIONS={self.basic_option} ./bin/colony-inspect",
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE, shell=True
        ) as proc:
            log_msg = proc.stdout.read().decode("utf-8")
            err_msg = proc.stderr.read().decode("utf-8")

        logs = glob.glob(f"xray-log/colony-inspect.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]

        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/colony-inspect {latest} > /tmp/xray-colony-inspect.yaml",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")

    def execute_native(self):

        with subprocess.Popen(
            f"./bin/colony-inspect",
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE, shell=True
        ) as proc:
            log_msg = proc.stdout.read().decode("utf-8")
            err_msg = proc.stderr.read().decode("utf-8")
            pattern = re.compile(r"Duration is (\d+)")
            entries = re.findall(pattern, log_msg)
            duration = int(entries[0])
            return duration

    def stop_repo(self):
        return

    def make_report(self, is_plot=False):
        self.runtime_report = DylinxRuntimeReport("/tmp/xray-colony-inspect.yaml")
        if is_plot:
            self.runtime_report.plot_lifetime_superposition_graph("./test.png")

class Colony:
    def __init__(self, allow_locks, sites):
        n_allow = len(allow_locks)
        n_site = len(sites)
        self.sites = sites
        self.combs = list(itertools.product(*([allow_locks] * n_site)))

    def load_sites(self, comb, n_total_sites, default_type="PTHREADMTX"):
        loaded = [default_type] * n_total_sites
        for i, s in enumerate(self.sites):
            loaded[s] = comb[i]
        return tuple(loaded)

    def store_topk(self, ranks, k=3):
        self.topk = []
        for i in range(k):
            self.topk.append(tuple(ranks[i][s] for s in self.sites))


if __name__ == "__main__":
    REPEAT_EXP = 5
    with ColonySubject("./compile_commands.json") as subject:
        sites = subject.get_pluggable()
        sorted_site_ids = sorted(sites.keys())
        if not os.path.exists("logs.pkl"):
            combs = list(itertools.product(*([ALLOWED_LOCK_TYPE] * len(sorted_site_ids))))
            logs = {}
            for comb in combs:
                id2type = {i: t for i, t in enumerate(comb)}
                subject.build_repo(id2type)
                durations = []
                for r in range(REPEAT_EXP):
                    dur = subject.execute_native()
                    durations.append(dur)
                logs[comb] = (np.mean(durations), np.std(durations))
                print(f"{comb} mean: {logs[comb][0]:.2e}, std: {logs[comb][1]:.2e}")
            with open("logs.pkl", "wb") as handler:
                pickle.dump(logs, handler)
        else:
            with open("logs.pkl", "rb") as handler:
                logs = pickle.load(handler)

    ranks = sorted(logs.keys(), key=lambda c: logs[c])
    comb2q_rank = {k: i for i, k in enumerate(ranks)}
    prev_mean = logs[ranks[0]][0]
    prev_std = logs[ranks[0]][1]
    q_i = 0
    for i, r in enumerate(ranks[1:]):
        if prev_mean + prev_std >= logs[r][0] >= prev_mean - prev_std:
            comb2q_rank[r] = q_i
        else:
            prev_mean = logs[ranks[i]][0]
            prev_std = logs[ranks[i]][1]
            q_i = i

    # LiTL search space
    litl_space = []
    for ltype in ALLOWED_LOCK_TYPE:
        litl_space.append((ltype,) * len(sorted_site_ids))
    litl_rank = sorted(litl_space, key=lambda c: logs[c][0])
    print(f"The real rank of best combination found by Litl is {ranks.index(litl_rank[0]):4d}/{len(ranks)}, q_rank: {comb2q_rank[litl_rank[0]]} {logs[litl_rank[0]][0]:3e}")

    colonies = [
        Colony(ALLOWED_LOCK_TYPE, [0, 1]),
        Colony(ALLOWED_LOCK_TYPE, [2, 3, 4]),
    ]
    final_best = ["PTHREADMTX"] * len(sorted_site_ids)
    for colony in colonies:
        local_space = [colony.load_sites(comb, 5) for comb in colony.combs]
        local_space = sorted(local_space, key=lambda c: logs[c][0])
        colony.store_topk(local_space)
    for group in itertools.product(*[list(range(len(c.topk))) for c in colonies]):
        comb = ()
        for i, c in enumerate(group):
            comb = comb + colonies[i].topk[c]
        print(f"rank #{group}: {ranks.index(comb):4d}/{len(ranks)}, q_rank: {comb2q_rank[comb]:4d} {logs[comb][0]:.2e}")

    # subject.execute_repo()
    # subject.make_report()
