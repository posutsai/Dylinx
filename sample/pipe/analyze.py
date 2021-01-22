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

class PipeSubject(BaseSubject):
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
            f"{os.environ['DYLINX_HOME']}/sample/pipe/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        cmd = f"make clean; make queue with_dlx=1"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self):

        with subprocess.Popen(
            f"XRAY_OPTIONS={self.xray_option} XRAY_BASIC_OPTIONS={self.basic_option} ./bin/queue",
            stderr=subprocess.PIPE,
            stdout=subprocess.PIPE, shell=True
        ) as proc:
            log_msg = proc.stdout.read().decode("utf-8")
            err_msg = proc.stderr.read().decode("utf-8")

        logs = glob.glob(f"xray-log/queue.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]

        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/queue {latest} > /tmp/xray-queue.yaml",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")

    def execute_native(self):

        with subprocess.Popen(
            f"./bin/queue",
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


if __name__ == "__main__":
    REPEAT_EXP = 5
    with PipeSubject("./compile_commands.json") as subject:
        sites = subject.get_pluggable()
        sorted_site_ids = sorted(sites.keys())
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

