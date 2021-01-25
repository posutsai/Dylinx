#!/usr/local/bin/python3
import sys
import glob
import os
import subprocess
import re
import itertools
import pickle
import numpy as np
import argparse
import matplotlib.pyplot as plt
from multiprocessing import cpu_count
if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, DylinxRuntimeReport, ALLOWED_LOCK_TYPE
from glob import glob

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

    def build_repo(self, id2type, n_thread=cpu_count()):
        super().configure_type(id2type)
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{os.environ['DYLINX_HOME']}/sample/pipe/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        cmd = f"make clean; make queue n_thread={n_thread} with_dlx=1"
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

def measure_scalability():
    op_per_tu = {}
    for t in [1, 2, 4, 8, 16, 32, 64]:
        cmd = f"make queue with_dlx=0 n_thread={t}; ./bin/queue"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        pattern = re.compile(r"Duration is (\d+)")
        entries = re.findall(pattern, log_msg)
        duration = int(entries[0]) / 1000
        op_per_tu[t] = (64 * 128 * 1024 + 64 * 128) / duration
    return op_per_tu

def plot_scalability(op_per_tu, fig_path):
    threads = sorted(op_per_tu.keys())
    measure = [op_per_tu[x] for x in threads]
    linear = [measure[0] * x for x in threads]
    fig, ax = plt.subplots()
    ax.set_title("Performance scalability in MPMC queue")
    ax.plot(threads, measure, label="actual", marker="o")
    ax.plot(threads, linear, label="linear", marker="o")
    plt.yscale("log")
    plt.xlabel("thread number")
    plt.ylabel(f"op per ms")
    ax.legend()
    plt.savefig(fig_path)

def str2bool(v):
    if isinstance(v, bool):
        return v
    if v.lower() in ('yes', 'true', 't', 'y', '1'):
        return True
    elif v.lower() in ('no', 'false', 'f', 'n', '0'):
        return False
    else:
        raise argparse.ArgumentTypeError('Boolean value expected.')

def brute_force(cc_path, pkl_path):
    with PipeSubject(cc_path) as subject:
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
        with open(pkl_path, "wb") as handler:
            pickle.dump(logs, handler)
def inspect_scalability(combs, cc_path, pkl_path, fig_path):
    with PipeSubject(cc_path) as subject:
        sites = subject.get_pluggable()
        sorted_site_ids = sorted(sites.keys())
        logs = {}
        n_threads = [1, 2, 4, 8, 16, 32, 64]
        for comb in combs:
            id2type = {i: t for i, t in enumerate(comb)}
            subject.build_repo(id2type)
            op_per_tu = {}
            for t in n_threads:
                subject.build_repo(id2type, t)
                durations = []
                for i in range(3):
                    durations.append(subject.execute_native() / 1000)
                op_per_tu[t] = (64 * 128 * 1024 + 64 * 128) / np.mean(durations)
            logs[comb] = op_per_tu
            with open(pkl_path, "wb") as handler:
                pickle.dump(logs, handler)
            fig, ax = plt.subplots()
            ax.set_title("Performance scalability among combinations in MPMC queue")
            for k in logs.keys():
                measure = [logs[k][t] for t in n_threads]
                ax.plot(n_threads, measure, label=f"{k}", marker="o")
            plt.xlabel("thread number")
            plt.ylabel(f"op per ms")
            ax.legend()
            plt.grid()
            plt.savefig(fig_path)
            print(f"{comb} completes")

def get_latest_id(file_ptn, re_ptn):
    existing_logs = sorted(glob(file_ptn))
    ptn = re.compile(re_ptn)
    log_ids = []
    for l in existing_logs:
        log_ids.append(int(ptn.findall(os.path.basename(l))[0]))
    save_id = 0 if len(log_ids) == 0 else max(log_ids)
    return save_id

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--plot_scalable",
        "-ps",
        type=str2bool,
        required=False,
        default=False,
    )
    parser.add_argument(
        "--brute_force",
        "-bf",
        type=str2bool,
        required=False,
        default=False,
    )
    parser.add_argument(
        "--comb_scalable",
        "-cs",
        type=str2bool,
        required=False,
        default=False,
    )
    args = parser.parse_args()
    REPEAT_EXP = 5
    if args.plot_scalable == True:
        op_per_tu = measure_scalability()
        plot_scalability(op_per_tu, "mpmc_scalability.png")

    elif args.brute_force == True:
        save_id = get_latest_id(
            "./save/exec_time/execution-*.pkl",
            r"execution-(\d)+.pkl"
        )
        brute_force(
            "./compile_commands.json",
            f"./save/comb2exetime-{save_id}.pkl"
        )

    elif args.comb_scalable == True:
        save_id = get_latest_id(
            "./save/scalability/scalability-*.pkl",
            r"scalability-(\d)+.pkl"
        )
        inspect_scalability(
            [ ("PTHREADMTX", "PTHREADMTX"), ("BACKOFF", "BACKOFF"), ("ADAPTIVEMTX", "BACKOFF")],
            "./compile_commands.json",
            f"./save/scalability/scalability-{save_id}.pkl",
            "mpmc_comb_scale.png"
        )
    else:
        print("Nothing has been done")
