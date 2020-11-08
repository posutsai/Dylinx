#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
import re
import itertools
import json

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, ALLOWED_LOCK_TYPE

LOCK_ABBREV = {
    "PTHREADMTX":'P',
    "ADAPTIVEMTX": 'A',
    "BACKOFF": 'B',
    "MCS": 'M',
    "TTAS": 'T'
}

def parse_comparison(benchmark, mode, run_file, comb):
    ptn = re.compile(r"### (.*) ###\n.*: ([\d.]*)x (slower|faster)\n(Not significant|Significant \(t=([\d.-]*)\))")
    result = {}
    with subprocess.Popen(
        f"pyperformance compare perf_log/{benchmark}/native.json perf_log/{benchmark}/{mode}/{run_file}".split(' '),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    ) as proc:
        stdout = proc.stdout.read().decode("utf-8")
        for m in re.finditer(ptn, stdout):
            result[m.group(1)] = {
                "comb": comb,
                "time_ratio": 1/float(m.group(2)) if m.group(3) == "slower" else float(m.group(2)),
                "significance": not m.group(4) == "Not significant"
            }
        if len(result.keys()) == 0:
            raise ValueError
    print(result)
    return result

class DylinxSubject(BaseSubject):
    def __init__(self, cc_path, bm):
        # self.bm_list = [
        #     "json_dumps",
        #     "nbody",
        #     "pickle_list",
        #     "regex_v8",
        #     "scimark"
        # ]
        self.bm_list = [ "nbody" ]
        self.repo = os.path.abspath(os.path.dirname(cc_path))
        self.sample_dir = os.path.abspath(os.path.dirname('.'))
        self.assure_standard(bm)
        super().__init__(cc_path)

    def assure_standard(self, bm):
        if bm == "pyperformance":
            if not os.path.isfile("./perf_log/pyperformance/native.json"):
                os.chdir(self.repo)
                proc = subprocess.Popen(
                    f"make; pyperformance run --python=./python -b {','.join(self.bm_list)} -o ../perf_log/pyperformance/native.json; /bin/rm -rf venv",
                    shell=True, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL
                )
                proc.wait()
                os.chdir("../")
        elif bm == "computation_bound":
            if not os.path.isfile("./perf_log/computation_bound/native.json"):
                os.chdir(self.repo)
        else:
            raise ValueError
        print("Standard performance checked")

    def build_repo(self, id2type):
        super().configure_type(id2type)
        cmd = f"cd repo; make -f dylinx.mk python3-dlx; cd .."
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        log_msg = proc.stdout.read().decode("utf-8")
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self, record_json):
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        if os.path.isfile(record_json):
            os.remove(record_json)
        os.chdir(self.repo)
        with subprocess.Popen(
            f"pyperformance run --python=./python3-dlx --inherit-environ=LD_LIBRARY_PATH -b {','.join(self.bm_list)} -o {record_json}; /bin/rm -rf venv",
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True
        ) as task:
            stdout = task.stdout.read().decode("utf-8")
            stderr = task.stderr.read().decode("utf-8")
        os.chdir("../")
        return stdout, stderr

    def stop_repo(self):
        return

def run(args):
    subject = DylinxSubject("./repo/compiler_commands.json", args.benchmark)
    sites = subject.get_pluggable()
    print(sites)
    if args.mode == "same":
        id2type = { s: args.ltype for s in sites.keys() }
        subject.build_repo(id2type)
        stdout, stderr = subject.execute_repo(f"{subject.sample_dir}/perf_log/{args.benchmark}/same/{args.ltype.lower()}.json")
        print(stdout)
        print(stderr)

    if args.mode == "grid":
        available_locks = ("PTHREADMTX", "BACKOFF", "TTAS")
        perm = itertools.product(*[available_locks] * len(sites.keys()))
        sorted_keys = sorted(sites.keys())
        log_dir = f"{subject.sample_dir}/perf_log/{args.benchmark}/grid"
        print(sorted_keys)
        result = []
        for p in perm:
            print(p)
            fmt_str = ''.join(list(map(lambda e: LOCK_ABBREV[p[e]], sorted_keys)))
            if not os.path.isfile(f"{log_dir}/{fmt_str}.json"):
                id2type = { k: p[i] for i, k in enumerate(sorted_keys) }
                subject.build_repo(id2type)
                stdout, stderr = subject.execute_repo(f"{log_dir}/{fmt_str}.json")
                print(f"permutation : {p} completed and save to {log_dir}/{fmt_str}.json")
            result.append(parse_comparison(args.benchmark, args.mode, f"{fmt_str}.json", list(p)))
        with open("grid_total.json", "w") as handler:
            json.dump(result, handler)





if __name__ == "__main__":
    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument(
        "-m",
        "--mode",
        type=str,
        choices=["same", "greedy", "grid"],
        required=True,
        help="Configuring the operating mode."
    )
    parser.add_argument(
        "-bm,",
        "--benchmark",
        type=str,
        choices=["pyperformance", "computation_bound"],
        default="pyperformance",
        help="Configuring the operating mode."
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

