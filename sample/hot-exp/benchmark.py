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
import multiprocessing

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, ALLOWED_LOCK_TYPE

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

def poisson_map(args):
    bins, critical_sections, bin_size = args
    stat = { n: 0 for n in range(multiprocessing.cpu_count())}
    start_index = 0
    for b in bins:
        contending = 0
        for cs in critical_sections[start_index:]:
            if cs.attempt > b + bin_size:
                break
            if (cs.attempt <= b and cs.deviate > b) or (cs.attempt <= b + bin_size and cs.deviate > b + bin_size):
                contending += 1
            if cs.deviate < b:
                start_index += 1
        stat[contending] = stat[contending] + 1 if contending in stat.keys() else stat.get(contending, 1)
    return stat

def get_poisson_lambda(critical_sections, bin_size=1000):
    durations = [cs.duration for cs in critical_sections]
    print(f"mean duration of cs: {np.mean(durations)}")
    critical_sections = sorted(critical_sections, key=lambda cs: cs.attempt)
    print(len(critical_sections))
    start_index = 0
    stat = { n: 0 for n in range(multiprocessing.cpu_count())}
    bins = list(range(critical_sections[0].attempt, critical_sections[-1].deviate, bin_size))
    print(f"bin number is {len(bins)}")
    residue = bins[-1 * (len(bins) % multiprocessing.cpu_count()): ]
    chop = len(bins) // multiprocessing.cpu_count()
    args = []
    for i in range(multiprocessing.cpu_count()):
        args.append((bins[chop * i: chop * (i + 1)], critical_sections, bin_size))
    args.append((residue, critical_sections, bin_size))
    parallel_res = g_pool.map(poisson_map, args)
    print("mapping complete")
    for k in stat.keys():
        stat[k] = sum([ p[k] for p in parallel_res ])
    accu = 0.
    print(stat)
    for n, occurrence in stat.items():
        accu += n * occurrence
    return accu / sum(stat.values())

def hot_map(args):
    start, critical_sections, cpu_count = args
    chop = len(critical_sections) // cpu_count
    stat = { n: [] for n in range(cpu_count) }
    for i in range(start * chop, (start + 1) * chop if start != cpu_count - 1 else (start + 1) * chop - 1):
        contending = 0
        switch_out = critical_sections[i]
        hot = critical_sections[i + 1].acquire - switch_out.release
        if hot < 0:
            print(switch_out)
            print(critical_sections[i + 1])
        for cs in critical_sections[i:]:
            if cs.attempt > switch_out.deviate:
                break
            if cs.attempt <= switch_out.release and cs.acquire > switch_out.release:
                contending += 1
        if contending not in stat.keys():
            stat[contending] = []
            stat[contending].append(hot)
        else:
            stat[contending].append(hot)
    return stat

def handover_time_eval(critical_sections):
    critical_sections = sorted(critical_sections, key=lambda cs: cs.acquire)
    print(f"number of cs: {len(critical_sections)}")
    result = g_pool.map(hot_map, [ (i, critical_sections, multiprocessing.cpu_count()) for i in range(multiprocessing.cpu_count())])
    stat = {n: [] for n in range(multiprocessing.cpu_count())}
    for r in result:
        for k in stat.keys():
            stat[k] = r[k] + stat[k]
    for n_thread, interval in stat.items():
        if len(interval) > 0:
            print(f"hot( {n_thread} ): occur = {len(interval)} mean = {np.mean(interval)}, std = {np.std(interval)}")
    return stat


class DylinxSubject(BaseSubject):
    def __init__(self, cc_path, cs_ratio):
        self.repo = os.path.abspath(os.path.dirname(cc_path))
        self.sample_dir = os.path.abspath(os.path.dirname('.'))
        self.xray_option = "\"patch_premain=true xray_mode=xray-basic xray_logfile_base=xray-log/ \""
        self.ideal_record = self.perform_ideal_poisson(cs_ratio)
        super().__init__(cc_path)

    def perform_ideal_poisson(self, cs_ratio):
        with subprocess.Popen(
            f"make contentionless cs_ratio={cs_ratio}; XRAY_OPTIONS={self.xray_option} ./bin/contentionless",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            pattern = re.compile(r"mean: ([\d.]*), std: ([\d.]*), max: ([\d.]*), min: ([\d.]*), main_tid: ([\d.]*)")
            stdout = proc.stdout.read().decode("utf-8")
            proc.stderr.read().decode("utf-8")
            mean, std, max, min, main_tid = re.findall(pattern, stdout)[0]
            print(f"mean: {mean}, std: {std}, max: {max}, min: {min}")
            if float(std) > 0.002:
                print("std value is too high. please consider to escalate the task duration.")
        logs = glob.glob("xray-log/contentionless.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]
        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/contentionless {latest} > /tmp/xray-ideal.yaml",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
        with open("/tmp/xray-ideal.yaml", "r") as stream:
            xray_log = stream.read()
            pattern = re.compile(r"- { type: 0, func-id: \d+, function: (.*), cpu: (\d*), thread: (\d*). process: (\d*), kind: function-(enter|exit), tsc: (\d*), data: '' }")
            entries = re.findall(pattern, xray_log)
            traces = list(map(lambda entry: TraceEntry(entry), entries))
            traces = list(filter(lambda trace: "critical" in trace.function, traces))
        critical_sections = group_cs(traces)
        self.ideal_lambda = get_poisson_lambda(critical_sections)
        print(f"Ideal poisson process is complete. lambda = {self.ideal_lambda}")
        sys.exit()


    def build_repo(self, ltype, cs_ratio, id2type):
        super().configure_type(id2type)
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        cmd = f"make clean; make single-hot cs_ratio={cs_ratio} ltype={ltype}"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self, ltype):
        proc = subprocess.Popen(f"XRAY_OPTIONS={self.xray_option} ./bin/single-hot-{ltype}", stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        logs = glob.glob(f"xray-log/single-hot-{ltype}.*")
        latest = sorted(logs, key=lambda l: os.path.getmtime(l))[-1]
        with subprocess.Popen(
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/single-hot-{ltype} {latest} > /tmp/xray-simple-hot.yaml",
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
        # self.practical_lambda = get_poisson_lambda(critical_sections)
        # print(f"{ltype} lambda is {self.practical_lambda}")
        handover_time_eval(critical_sections)

    def stop_repo(self):
        return

def run(args):

    subject = DylinxSubject("./compiler_commands.json", args.cs_ratio)
    sites = subject.get_pluggable()

    if args.mode == "single":
        sorted_keys = sorted(sites.keys())
        id2type = { k: args.ltype for i, k in enumerate(sorted_keys) }
        print(id2type)
        subject.build_repo(args.ltype, args.cs_ratio, id2type)
        subject.execute_repo(args.ltype)

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
    parser.add_argument(
        "-cs",
        "--cs_ratio",
        type=float,
        required=True,
        help="Configuring the critical section ratio."
    )
    parser.add_argument(
        "-b",
        "--bin",
        type=int,
        required=True,
        help="Configuring the time duration unit of bin size when getting lambda"
    )
    parser.add_argument(
        "-lt",
        "--ltype",
        type=str,
        choices=ALLOWED_LOCK_TYPE,
        help="Configure lock type."
    )
    args = parser.parse_args()
    run(args)

