#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
import re
import itertools
import json
import numpy as np
import glob
import yaml
import pickle
import multiprocessing
import cmath
import math
from scipy.stats import anderson

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

def solve_quadratic(a, b, c):
    dis = (b**2) - (4 * a*c)
    sol_0 = (-b + cmath.sqrt(dis))/(2 * a)
    sol_1 = (-b - cmath.sqrt(dis))/(2 * a)
    return sol_0, sol_1

def compute_utility(l_eq):
    def rho2serv_dur(rho):
        serv_rate = arr_rate / rho
        return 1. / serv_rate
    rho_0, rho_1 = solve_quadratic(1, (-2 * l_eq - 2), 2 * l_eq)
    assert(rho_0.imag == 0 and rho_1.imag == 0)
    print(f"rho = ({rho_0}, {rho_1})")
    return max(rho_0.real, rho_1.real)

def poisson_prob(l, duration, k):
    m = duration * l
    return ((m ** k) * math.exp(-1 * m)) / math.factorial(k)

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


class DylinxSubject(BaseSubject):

    def __init__(self, cc_path, cs_ratio, cs_exp, bin_size):
        self.repo = os.path.abspath(os.path.dirname(cc_path))
        self.sample_dir = os.path.abspath(os.path.dirname('.'))
        self.xray_option = "\"patch_premain=true xray_mode=xray-basic xray_logfile_base=xray-log/ \""
        self.ideal_cs_ratio = cs_ratio
        self.bin_size = bin_size
        self.cs_exp = cs_exp
        self.corelation = {
            "x": [],
            "y": [],
        }
        self.hyper_param = self.measure_ideal_ratio(cs_ratio, cs_exp)
        self.ideal_arrival_avg, self.ideal_possess_avg = self.measure_ideal_cs(cs_ratio, cs_exp)
        #! Reconsider whether to add bin size
        self.ideal_utility = self.ideal_arrival_avg * self.ideal_possess_avg
        print(f"ideal_lambda={self.ideal_arrival_avg} ideal utility is {self.ideal_utility}")
        super().__init__(cc_path)

    def __enter__(self):
        if self.ideal_cs_ratio != 1.:
            print("Hyperparameter within lockless process is complete")
            print(f"ideal cs_ratio={self.ideal_cs_ratio:3.2f}, cs_duration={2 ** (self.cs_exp - 1)}")
            cs_ratio = self.hyper_param['cs_ratio']
            print(f"measured cs_ratio max={cs_ratio['max']}, min={cs_ratio['min']}, mean={cs_ratio['mean']}, std={cs_ratio['std']}")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.revert_repo()
        pass

    def measure_ideal_ratio(self, cs_ratio, cs_exp):
        hyper_param = {}
        with subprocess.Popen(
            f"make ratio-measure cs_ratio={cs_ratio:1.2f} cs_exp={cs_exp}; ./bin/ratio-measure",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            pattern = re.compile(r"mean: ([\d.]*), std: ([\d.]*), max: ([\d.]*), min: ([\d.]*), main_tid: ([\d.]*)")
            stdout = proc.stdout.read().decode("utf-8")
            proc.stderr.read().decode("utf-8")
            ratio_log = {}
            ratio_log["mean"], ratio_log["std"], ratio_log["max"], ratio_log["min"], _ = re.findall(pattern, stdout)[0]
            hyper_param["cs_ratio"] = ratio_log
        return hyper_param

    def measure_ideal_cs(self, cs_ratio, cs_exp):
        with subprocess.Popen(
            f"make lockless cs_ratio={cs_ratio:1.2f} cs_exp={cs_exp}; XRAY_OPTIONS={self.xray_option} ./bin/lockless",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            log_msg = proc.stdout.read().decode("utf-8")
            err_msg = proc.stderr.read().decode("utf-8")
        logs = glob.glob(f"xray-log/lockless.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]
        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/lockless {latest} > /tmp/xray-lockless.yaml",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
        with open("/tmp/xray-lockless.yaml") as stream:
            xray_log = stream.read()
            pattern = re.compile(r"- { type: 0, func-id: \d+, function: (.*), cpu: (\d*), thread: (\d*). process: (\d*), kind: function-(enter|exit), tsc: (\d*), data: '' }")
            entries = re.findall(pattern, xray_log)
            traces = list(map(lambda entry: TraceEntry(entry), entries))
            traces = list(filter(lambda trace: "critical" in trace.function, traces))
        critical_sections = group_cs(traces)
        arrival_avg, possession_avg, duration_avg = count_mean_by_op(critical_sections, self.bin_size, arrival_cmp)
        return arrival_avg, possession_avg


    def build_repo(self, ltype, cs_ratio, id2type, cs_exp):
        super().configure_type(id2type)
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        cmd = f"make clean; make single-hot cs_ratio={cs_ratio} cs_exp={cs_exp} with_dlx=1"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self, ltype):
        with subprocess.Popen("time ./bin/single-hot", stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True) as proc:
            pattern = re.compile(r"duration ([\d.]*)s")
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
            r = re.findall(pattern, stdout)[0]
            print(r)
            real_perf = float(r)
            g_corelation["real_perf"].append(real_perf)
        proc = subprocess.Popen(f"XRAY_OPTIONS={self.xray_option} ./bin/single-hot", stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        logs = glob.glob(f"xray-log/single-hot.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]
        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/single-hot {latest} > /tmp/xray-simple-hot.yaml",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
        with open("/tmp/xray-simple-hot.yaml") as stream:
            xray_log = stream.read()
            pattern = re.compile(r"- { type: 0, func-id: \d+, function: (.*), cpu: (\d*), thread: (\d*). process: (\d*), kind: function-(enter|exit), tsc: (\d*), data: '' }")
            entries = re.findall(pattern, xray_log)
            traces = list(map(lambda entry: TraceEntry(entry), entries))
            traces = list(filter(lambda trace: "critical" in trace.function, traces))
        critical_sections = group_cs(traces)
        self.system_entity, possess_avg, duration_avg = count_mean_by_op(critical_sections, self.bin_size, overlap_cmp)
        print(f"practical_lambda = {self.system_entity}")
        self.utility = compute_utility(self.system_entity)
        self.hot = handover_time_eval(critical_sections)
        total_handover_times = (2 ** 8) * 64 -1
        arrival_rate = self.utility / self.ideal_possess_avg
        sum_hot = 0.
        for n_thread, props in self.hot.items():
            p = poisson_prob(arrival_rate, self.ideal_possess_avg, n_thread)
            sum_hot += p * props["mean"]
        with open("save/indicator.log", "a") as handler:
            handler.write(
                f"lock={ltype:12s}, "
                f"ideal_ratio={self.ideal_cs_ratio:2.2f}, mean_ratio={self.hyper_param['cs_ratio']['mean']}, exp={2**self.cs_exp:8d}, "
                f"sum_hot={sum_hot:9.4f}, occupy_ratio={sum_hot / self.ideal_possess_avg:2.6f} "
                f"utility={self.utility:4.2f}, rho_ratio={self.utility / self.ideal_utility: 2.6f}\n"
            )
        g_corelation["occupy_ratio"].append(sum_hot / self.ideal_possess_avg)
        g_corelation["rho_ratio"].append(self.utility / self.ideal_utility)


    def stop_repo(self):
        return

def run(args):

    if args.mode == "single":
        print("Start experiment with no parallel section and tune duration exp from 10 ~ 20")
        result = {
            "no_parallel": {}
        }
        open("save/indicator.log", "w").close()
        for ratio in np.arange(0.2, 1.2, 0.2):
            for exp in np.arange(10, 14, 1):
                result["no_parallel"][exp] = {}
                with DylinxSubject("./compiler_commands.json", ratio, exp, 2000) as subject:
                    sites = subject.get_pluggable()
                    lock_types = ["PTHREADMTX", "TTAS", "BACKOFF"]
                    sorted_keys = sorted(sites.keys())
                    for lt in lock_types:
                        id2type = { k: lt for i, k in enumerate(sorted_keys) }
                        subject.build_repo(lt, ratio, id2type, exp)
                        subject.execute_repo(lt)
                        result["no_parallel"][exp][lt] = {
                            "hot": subject.hot,
                            # "utility": subject.utility,
                        }
                    with open("exp-log.pkl", "wb") as pfile:
                        pickle.dump(result, pfile)
                    print(f"Complete exp {exp}")

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

