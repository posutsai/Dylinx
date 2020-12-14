#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
import re
import json
import numpy as np
import glob
import yaml
import pickle
import multiprocessing
import math
import matplotlib.pyplot as plt
from scipy.optimize import newton
from q_network import MachineRepairGeneralQueue, solve_lock_overhead, compute_response_time

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, ALLOWED_LOCK_TYPE

g_corelation = {
    "real_perf": [],
    "occupy_ratio": [],
    "rho_ratio": [],
}

class TraceEntry:
    def __init__(self, groups):
        self.function = groups[0]
        self.cpu = int(groups[1])
        self.thread = int(groups[2])
        self.process = int(groups[3])
        self.kind = groups[4]
        self.tsc = int(groups[5])
    def __str__(self):
        return f"{self.function} ({self.kind}) cpu:{self.cpu}, thread:{self.thread}, tsc:{self.tsc}"

class CodeSection:
    def __init__(self, entry, exit):
        self.entry = entry.tsc
        self.exit = exit.tsc
        self.duration = self.exit - self.entry

class CriticalSection:
    def __init__(self, timestamps):
        attempt, acquire, release, deviate = timestamps
        assert(attempt.function == "critical_section" and attempt.kind == "enter")
        assert(acquire.function == "critical_load" and acquire.kind == "enter")
        assert(release.function == "critical_load" and release.kind == "exit")
        assert(deviate.function == "critical_section" and deviate.kind == "exit")
        self.attempt = timestamps[0].tsc
        self.acquire = timestamps[1].tsc
        self.release = timestamps[2].tsc
        self.deviate = timestamps[3].tsc
        self.duration = self.deviate - self.attempt
        self.possession = self.release - self.acquire
        self.tid = timestamps[0].thread
        self.pid = timestamps[0].process
        assert(self.duration > 0 and self.possession > 0)

    def __str__(self):
        props = {
            "tsc": {
                "attempt": self.attempt,
                "acquire": self.acquire,
                "release": self.release,
                "deviate": self.deviate,
            },
            "duration": self.duration,
            "possession": self.possession
        }
        return str(props)

def group_cs(records):
    threads = {r.thread for r in records}
    logs = {t: [] for t in threads}
    for r in records:
        logs[r.thread].append(r)
    critical_sections = []
    for t in logs.keys():
        ordered = sorted(logs[t], key=lambda r: r.tsc)
        assert(len(ordered) % 4 == 0)
        for i in range(len(ordered) // 4):
            critical_sections.append(CriticalSection(ordered[i*4: i*4 + 4]))
    return critical_sections

def check_arrival_dist(records):
    threads = {r.thread for r in records}
    logs = {t: [] for t in threads}
    for r in records:
        logs[r.thread].append(r)
    parallel_ops = []
    for t in logs.keys():
        ordered = sorted(logs[t], key=lambda r: r.tsc)
        # print(ordered[0])
        # print(ordered[1])
        # sys.exit()
        assert(len(ordered) % 2 == 0)
        for i in range(len(ordered) // 2):
            parallel_ops.append(CodeSection(ordered[i * 2], ordered[i * 2 + 1]))
    durations = [op.duration for op in parallel_ops]
    return np.mean(durations) / np.std(durations), np.mean(durations), np.std(durations)

class OverheadMeasurement:
    def __init__(self, ratios, n_cores, lock_types, repeat):
        self.ratios = ratios
        self.n_cores = n_cores
        self.lock_types = lock_types
        self.repeat = repeat
        self.result = { l:{} for l in lock_types }
        self.result["ideal"] = []

    def conduct(self):
        for ratio in self.ratios:
            for n_core in self.n_cores:
                setting = (ratio, n_core)
                with MachineRepairSubject("./compile_commands.json", ratio, n_core, 2000) as subject:
                    subject.measure_theoretical_resp()
                    sites = subject.get_pluggable()
                    sorted_keys = sorted(sites.keys())
                    for lt in self.lock_types:
                        overheads = []
                        waitings = []
                        id2type = { k: lt for i, k in enumerate(sorted_keys) }
                        subject.build_repo(lt, ratio, id2type, n_core)
                        for r in range(self.repeat):
                            oh, w = subject.execute_repo(lt)
                            overheads.append(oh)
                            waitings.append(w)
                        print(f"{lt} {setting} overhead: {np.mean(overheads)}, {np.std(overheads):.2f}")
                        self.result[lt][setting] = {"overhead": (np.mean(overheads), np.std(overheads)), "waiting": np.mean(waitings)}
                    self.result["ideal_waiting"] = subject.theoretical_resp
            self.plot_figure(ratio)


    def plot_figure(self, ratio):
    # Expected result format
    # {
    #   "PTHREADMTX": {
    #       (n_core, ratio): overhead,
    #       ....
    #   },
    #   ....
    # }
        fig, ax = plt.subplots()
        ax.set_title(f"Overhead of different locks under ratio {ratio}")
        # ax.set_ylim(0, 40000)
        for lt in self.lock_types:
            overheads = []
            stds = []
            for n_core in self.n_cores:
                overheads.append(self.result[lt][(ratio, n_core)]["overhead"][0])
                stds.append(self.result[lt][(ratio, n_core)]["overhead"][1])
            overheads = np.array(overheads)
            stds = np.array(stds)
            line = ax.plot(self.n_cores, overheads, label=f"{lt}", marker=".")
            ax.fill_between(self.n_cores, overheads + stds, overheads - stds, alpha=0.3)
        ax.legend()
        plt.savefig(f"figure/overhead_{ratio}.png")

        fig, ax = plt.subplots()
        ax.set_title(f"Waiting time of different locks under ratio {ratio}")
        for lt in self.lock_types:
            waitings = []
            for n_core in self.n_cores:
                waitings.append(self.result[lt][(ratio, n_core)]["waiting"])
            line = ax.plot(self.n_cores, waitings, label=f"{lt}")
        ax.legend()
        plt.savefig(f"figure/waiting_{ratio}.png")

    def generate_table(self):
        # 1. save to pickle file.
        # 2. write human readable file.
        pass

# Compare functions only allow to output multiple kinds of values, depending
# on costomized logic
# A. output =  O: Satisfy break condition
# B. output =  1: Increment counter
# C. output =  2: Shift pointer
# D. output = -1: Default return
def arrival_cmp(b, bin_size, cs):
    if b <= cs.attempt < b + bin_size:
        return 1
    if cs.attempt > b + bin_size:
        return 0
    return -1

def overlap_cmp(b, bin_size, cs):
    if cs.attempt > b + bin_size:
        return 0
    if (cs.attempt <= b and cs.deviate > b) or (cs.attempt <= b + bin_size and cs.deviate > b + bin_size):
        return 1
    return -1

def poisson_map(args):
    bins, critical_sections, bin_size, compare_op = args
    stat = { n: 0 for n in range(multiprocessing.cpu_count())}
    start_index = 0
    for b in bins:
        contending = 0
        for cs in critical_sections[start_index:]:
            op_output = compare_op(b, bin_size, cs)
            if op_output == 0:
                break
            if op_output == 1:
                contending += 1
            if op_output == 2:
                start_index += 1
        stat[contending] = stat[contending] + 1 if contending in stat.keys() else stat.get(contending, 1)
    return stat

def count_mean_by_op(critical_sections, bin_size, compare_op):
    durations = [cs.duration for cs in critical_sections]
    possessions = [cs.possession for cs in critical_sections]
    print(f"mean duration of cs: {np.mean(durations):12.2f}")
    print(f"mean possession of cs: {np.mean(possessions):12.2f}")
    critical_sections = sorted(critical_sections, key=lambda cs: cs.attempt)
    start_index = 0
    stat = { n: 0 for n in range(multiprocessing.cpu_count())}
    bins = list(range(critical_sections[0].attempt, critical_sections[-1].deviate, bin_size))
    residue = bins[-1 * (len(bins) % multiprocessing.cpu_count()): ]
    chop = len(bins) // multiprocessing.cpu_count()
    args = []
    for i in range(multiprocessing.cpu_count()):
        args.append((bins[chop * i: chop * (i + 1)], critical_sections, bin_size, compare_op))
    args.append((residue, critical_sections, bin_size, compare_op))
    parallel_res = g_pool.map(poisson_map, args)
    for k in stat.keys():
        stat[k] = sum([ p[k] for p in parallel_res ])
    accu = 0.
    print(stat)
    for n, occurrence in stat.items():
        accu += n * occurrence
    return accu / sum(stat.values()), np.mean(possessions), np.mean(durations)

def hot_map(args):
    start, critical_sections, cpu_count, acquire_order, attempt_order = args
    chop = len(critical_sections) // cpu_count
    stat = { n: [] for n in range(cpu_count) }
    scan_origin = 0
    shift = critical_sections[attempt_order[0]].attempt

    for i in range(start * chop, (start + 1) * chop if start != cpu_count - 1 else (start + 1) * chop - 1):
        contending = 0
        switch_out = critical_sections[acquire_order[i]]
        for attempt_i in attempt_order[scan_origin:]:
            scanning_cs = critical_sections[attempt_i]
            if scanning_cs.attempt > switch_out.deviate:
                break
            if scanning_cs.attempt <= switch_out.release and scanning_cs.acquire > switch_out.release:
                contending += 1
        if contending == 0:
            hot = critical_sections[acquire_order[i + 1]].acquire - critical_sections[acquire_order[i + 1]].attempt
        else:
            hot = critical_sections[acquire_order[i + 1]].acquire - switch_out.release
        if hot < 0:
            print(switch_out)
            print(critical_sections[i + 1])
        if contending not in stat.keys():
            stat[contending] = []
            stat[contending].append(hot)
        else:
            stat[contending].append(hot)
    return stat

def handover_time_eval(critical_sections):
    acquire_order = list(range(len(critical_sections)))
    acquire_order = sorted(acquire_order, key=lambda i: critical_sections[i].acquire)
    attempt_order = list(range(len(critical_sections)))
    attempt_order = sorted(attempt_order, key=lambda i: critical_sections[i].attempt)
    result = g_pool.map(hot_map, [ (i, critical_sections, multiprocessing.cpu_count(), acquire_order, attempt_order) for i in range(multiprocessing.cpu_count())])
    stat = {n: [] for n in range(multiprocessing.cpu_count())}
    for r in result:
        for k in stat.keys():
            stat[k] = r[k] + stat[k]
    hot_info = {}
    for n_thread, interval in stat.items():
        if len(interval) > 0:
            hot_info[n_thread] = {
                "occur": len(interval),
                "mean": np.mean(interval),
                "std": np.std(interval),
            }
            print(f"hot({n_thread:3d}) occur: {len(interval):8d} mean: {np.mean(interval):9.2f}, std: {np.std(interval):9.2f}, max: {max(interval):10d}, min: {min(interval):8d}")
        else:
            hot_info[n_thread] = {
                "occur": 0,
                "mean": 0.,
                "std": 0.,
            }
    return hot_info


class MachineRepairSubject(BaseSubject):

    def __init__(self, cc_path, cs_ratio, n_core, bin_size):
        self.repo = os.path.abspath(os.path.dirname(cc_path))
        self.sample_dir = os.path.abspath(os.path.dirname('.'))
        self.xray_option = "\"patch_premain=true xray_mode=xray-basic xray_logfile_base=xray-log/ \""
        self.basic_option = "\"func_duration_threshold_us=0\""
        self.ideal_cs_ratio = cs_ratio
        self.n_core = n_core
        self.bin_size = bin_size
        self.ideal_duration_avg, self.ideal_possess_avg, self.parallel_time = self.measure_ideal_cs(cs_ratio, n_core)
        print(f"mean duration = {self.ideal_duration_avg:10.2f}, possession = {self.ideal_possess_avg:10.2f}")
        self.q_model = MachineRepairGeneralQueue(self.ideal_possess_avg, self.parallel_time, n_core)
        super().__init__(cc_path)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.revert_repo()

    def measure_theoretical_resp(self):
        self.theoretical_resp = compute_response_time(self.ideal_possess_avg, self.parallel_time, self.n_core)

    def measure_ideal_cs(self, cs_ratio, n_core):
        with subprocess.Popen(
            f"make ideal-measure cs_ratio={cs_ratio:1.2f} n_core=1; XRAY_OPTIONS={self.xray_option} XRAY_BASIC_OPTIONS={self.basic_option} ./bin/ideal-measure",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            log_msg = proc.stdout.read().decode("utf-8")
            err_msg = proc.stderr.read().decode("utf-8")
        logs = glob.glob(f"xray-log/ideal-measure.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]
        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/ideal-measure {latest} > /tmp/xray-ideal-measure.yaml",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
        with open("/tmp/xray-ideal-measure.yaml") as stream:
            xray_log = stream.read()
            pattern = re.compile(r"- { type: 0, func-id: \d+, function: (.*), cpu: (\d*), thread: (\d*). process: (\d*), kind: function-(enter|exit), tsc: (\d*), data: '' }")
            entries = re.findall(pattern, xray_log)
            traces = list(map(lambda entry: TraceEntry(entry), entries))
            critical_traces = list(filter(lambda trace: "critical" in trace.function, traces))
            parallel_traces = list(filter(lambda trace: "parallel" in trace.function, traces))
        critical_sections = group_cs(critical_traces)
        _, parallel_time, _ = check_arrival_dist(parallel_traces)
        return np.mean([cs.duration for cs in critical_sections]), np.mean([cs.possession for cs in critical_sections]), parallel_time


    def build_repo(self, ltype, cs_ratio, id2type, n_core):
        super().configure_type(id2type)
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        cmd = f"make clean; make lock-overhead cs_ratio={cs_ratio} n_core={n_core} with_dlx=1"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self, ltype):
        with subprocess.Popen("time ./bin/lock-overhead", stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
        proc = subprocess.Popen(f"XRAY_OPTIONS={self.xray_option} XRAY_BASIC_OPTIONS={self.basic_option} ./bin/lock-overhead", stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        logs = glob.glob(f"xray-log/lock-overhead.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]
        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/lock-overhead {latest} > /tmp/xray-lock-overhead.yaml",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
        with open("/tmp/xray-lock-overhead.yaml") as stream:
            xray_log = stream.read()
            pattern = re.compile(r"- { type: 0, func-id: \d+, function: (.*), cpu: (\d*), thread: (\d*). process: (\d*), kind: function-(enter|exit), tsc: (\d*), data: '' }")
            entries = re.findall(pattern, xray_log)
            traces = list(map(lambda entry: TraceEntry(entry), entries))
            critical_traces = list(filter(lambda trace: "critical" in trace.function, traces))
            parallel_traces = list(filter(lambda trace: "parallel" in trace.function, traces))
        critical_sections = group_cs(critical_traces)
        exp_fit, parallel_mean, _ = check_arrival_dist(parallel_traces)
        # print(f"exponential fitness = {exp_fit}")
        real_duration_avg = np.mean([cs.deviate - cs.attempt for cs in critical_sections])
        real_possession_avg = np.mean([cs.release - cs.acquire for cs in critical_sections])
        print(f"actual ratio {real_possession_avg / parallel_mean:.2f}")
        print(f"real duration = {real_duration_avg:10.2f}, possession = {real_possession_avg:10.2f}")
        overhead = newton(solve_lock_overhead, 10000, args=[self.q_model, real_duration_avg])
        return overhead, np.mean([cs.acquire - cs.attempt for cs in critical_sections])


    def stop_repo(self):
        return

def run(args):

    if args.mode == "single":
        print("Start experiment with no parallel section and tune duration exp from 10 ~ 20")
        result = {
            "no_parallel": {}
        }
        open("save/indicator.log", "w").close()
        # n_cores = list(range(2, multiprocessing.cpu_count(), 32))
        # n_cores.append(multiprocessing.cpu_count())
        n_cores = [8, 16, 24, 32, 48, 64]
        # n_cores = [8, 16]
        measurement = OverheadMeasurement(
            # [2 ** e for e in range(-3, 0)],
            [0.25, 0.5, 0.75, 1],
            n_cores,
            # ["TTAS"],
            ["PTHREADMTX", "TTAS", "BACKOFF"],
            8
        )
        measurement.conduct()

    elif args.mode == "mix":
        raise NotImplemented

g_pool = multiprocessing.Pool(multiprocessing.cpu_count())

if __name__ == "__main__":
    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument(
        "-m",
        "--mode",
        type=str,
        choices=["single", "mix"],
        required=True,
        help="Configuring the operating mode."
    )
    args = parser.parse_args()
    run(args)
    with open("corelation_data.pkl", "wb") as pfile:
        pickle.dump(g_corelation, pfile)

