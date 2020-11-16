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

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, ALLOWED_LOCK_TYPE

class CriticalSection:
    def __init__(self, enter, exit):
        self.start = enter["tsc"]
        self.end = exit["tsc"]
        self.duration = self.end - self.start
        self.tid = enter["thread"]
        self.pid = enter["process"]
        assert(self.duration > 0)
        assert(enter["thread"] == exit["thread"])

def get_poisson_lambda(records, main_tid, bin_size=1000000):
    records = list(filter(lambda r: r["function"] == "critical_section", records))
    threads = {r["thread"] for r in records}
    assert(main_tid in threads)
    logs = {t: [] for t in threads}
    for r in records:
        logs[r["thread"]].append(r)
    logs.pop(main_tid, None)
    critical_sections = []
    for t in logs.keys():
        ordered = sorted(logs[t], key=lambda r: r["tsc"])
        assert(len(ordered) % 2 == 0)
        for i in range(len(ordered) // 2):
            critical_sections.append(CriticalSection(ordered[i*2], ordered[i*2 + 1]))
    critical_sections = sorted(critical_sections, key=lambda cs: cs.end)
    scanned_index = 0
    statistics = { n: 0 for n in range(len(threads))}
    for bin in range(critical_sections[0].start, critical_sections[-1].end, bin_size):
        contending_num = 0
        for cs in critical_sections[scanned_index:]:
            if cs.start > bin + bin_size:
                break
            if cs.start <= bin and cs.end > bin:
                contending_num += 1
                scanned_index += 1
        statistics[contending_num] += 1
    accu = 0.
    for n, occurrence in statistics.items():
        accu += n * occurrence
    return accu / sum(statistics.values())

class DylinxSubject(BaseSubject):
    def __init__(self, cc_path, cs_ratio):
        self.repo = os.path.abspath(os.path.dirname(cc_path))
        self.sample_dir = os.path.abspath(os.path.dirname('.'))
        self.xray_option = "XRAY_OPTIONS=\"patch_premain=true xray_mode=xray-basic xray_logfile_base=xray-log/\""
        self.ideal_record = self.perform_ideal_poisson(cs_ratio)
        sys.exit()
        super().__init__(cc_path)

    def perform_ideal_poisson(self, cs_ratio):
        with subprocess.Popen(
            f"make contentionless cs_ratio={cs_ratio}; {self.xray_option} ./bin/contentionless;",
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
            f"llvm-xray convert -f yaml -symbolize -instr_map=./bin/contentionless {latest}",
            shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE
        ) as proc:
            stdout = proc.stdout.read().decode("utf-8")
            stderr = proc.stderr.read().decode("utf-8")
            xray_recr = yaml.safe_load(stdout)
            self.cycle_freq = xray_recr["header"]["cycle-frequency"]
            self.ideal_lambda = get_poisson_lambda(xray_recr["records"], int(main_tid))
        print(f"Ideal poisson process is complete. lambda = {self.ideal_lambda}")

    def build_repo(self, id2type):
        super().configure_type(id2type)
        cmd = f"cd repo; make -f dylinx.mk python3-dlx; cd .."
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self, benchmark, record_json=None):
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        os.chdir(self.repo)
        result = None
        if benchmark == "pyperformance":
            with subprocess.Popen(
                f"pyperformance run --python=./python3-dlx --inherit-environ=LD_LIBRARY_PATH -b {','.join(self.bm_list)} -o {record_json}; /bin/rm -rf venv",
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True
            ) as task:
                stdout = task.stdout.read().decode("utf-8")
                stderr = task.stderr.read().decode("utf-8")
                result = {
                    "stdout": stdout,
                    "stderr": stderr,
                }
        elif benchmark == "computation_bound":
            durations = []
            for i in range(5):
                with subprocess.Popen( f"./python3-dlx ../cpu_bound_task.py", shell=True, stderr=subprocess.PIPE, stdout=subprocess.PIPE) as proc:
                    stderr = proc.stderr.read().decode("utf-8")
                    if stderr != "":
                        print(stderr)
                    durations.append(float(proc.stdout.read().decode("utf-8")))
            result = {
                "mean": np.mean(durations),
                "std": np.std(durations)
            }
        else:
            raise ValueError
        os.chdir("../")
        return result

    def stop_repo(self):
        return

def run(args):
    subject = DylinxSubject("./compiler_commands.json", args.cs_ratio)
    sites = subject.get_pluggable()
    print(sites)
    sys.exit()
    if args.mode == "same":
        id2type = { s: args.ltype for s in sites.keys() }
        subject.build_repo(id2type)
        if args.benchmark == "pyperformance":
            result = subject.execute_repo(f"{subject.sample_dir}/perf_log/{args.benchmark}/same/{args.ltype.lower()}.json")
            print(result["stdout"])
            print(result["stderr"])
        elif args.benchmark == "computation_bound":
            result = subject.execute_repo(f"")

    if args.mode == "grid":
        available_locks = ("PTHREADMTX", "BACKOFF", "TTAS")
        perm = itertools.product(*[available_locks] * len(sites.keys()))
        sorted_keys = sorted(sites.keys())
        log_dir = f"{subject.sample_dir}/perf_log/{args.benchmark}/grid"
        print(sorted_keys)
        if args.benchmark == "pyperformance":
            result = []
            for p in perm:
                print(p)
                fmt_str = ''.join(list(map(lambda e: LOCK_ABBREV[p[e]], sorted_keys)))
                if not os.path.isfile(f"{log_dir}/{fmt_str}.json"):
                    id2type = { k: p[i] for i, k in enumerate(sorted_keys) }
                    subject.build_repo(id2type)
                    subject.execute_repo(args.benchmark, f"{log_dir}/{fmt_str}.json")
                    print(f"permutation : {p} completed and save to {log_dir}/{fmt_str}.json")
                    result.append(parse_comparison(args.benchmark, args.mode, f"{fmt_str}.json", list(p)))
            with open("{log_dir}total.json", "w") as handler:
                json.dump(result, handler)

        elif args.benchmark == "computation_bound":
            result = {}
            result["native"] = subject.native_record
            for p in perm:
                print(p)
                fmt_str = ''.join(list(map(lambda e: LOCK_ABBREV[p[e]], sorted_keys)))
                id2type = { k: p[i] for i, k in enumerate(sorted_keys) }
                subject.build_repo(id2type)
                result[fmt_str] = subject.execute_repo(args.benchmark)
                print(result[fmt_str])
            with open(f"{log_dir}/grid.json", "w") as handler:
                json.dump(result, handler)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument(
        "-m",
        "--mode",
        type=str,
        choices=["ideal", "single", "mix"],
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

