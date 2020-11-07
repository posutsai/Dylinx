#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
import re

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, ALLOWED_LOCK_TYPE

def parse_comparison(std_file, run_file):
    ptn = re.compile(r"### (.*) ###\n.*\n(Not significant|Significant \(t=([\d.-]*)\))")
    result = {}
    with subprocess.Popen(
        f"pyperformance compare perf_log/{std_file} perf_log/{run_file}".split(' '),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    ) as proc:
        stdout = proc.stdout.read().decode("utf-8")
        record = parse(stdout)
        if len(record.keys()) == 0:
            raise ValueError
        for m in re.finditer(ptn, stdout):
            result[m.group(1)] = {
                "ratio": float(m.group(2)),
                "slower/faster": m.group(3),
                "significance": not m.group(4) == "Not significant"
            }
    return result

class DylinxSubject(BaseSubject):
    def __init__(self, cc_path):
        self.bm_list = [
            "json_dumps",
            "nbody",
            "pickle_list",
            "regex_v8",
            "scimark"
        ]
        self.repo = os.path.abspath(os.path.dirname(cc_path))
        self.sample_dir = os.path.abspath(os.path.dirname('.'))
        self.assure_standard()
        super().__init__(cc_path)

    def assure_standard(self):
        if not os.path.isfile("./perf_log/native_pthreadmtx.json"):
            os.chdir(self.repo)
            proc = subprocess.Popen(
                f"make; pyperformance run --python=./python -b {','.join(self.bm_list)} -o ../perf_log/native_pthreadmtx.json; /bin/rm -rf venv",
                shell=True, stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL
            )
            proc.wait()
            os.chdir("../")
        print("Standard performance checked")

    def build_repo(self, id2type):
        super().configure_type(id2type)
        cmd = f"cd repo; make -f dylinx.mk python3-dlx; cd .."
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
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
    subject = DylinxSubject("./repo/compiler_commands.json")
    sites = subject.get_pluggable()
    if args.mode == "same":
        id2type = { s: args.ltype for s in sites.keys() }
        subject.build_repo(id2type)
        stdout, stderr = subject.execute_repo(f"{subject.sample_dir}/perf_log/same/{args.ltype.lower()}.json")
        print(stdout)
        print(stderr)


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
        "-lt",
        "--ltype",
        type=str,
        required=True,
        choices=ALLOWED_LOCK_TYPE,
        help="Configure lock type."
    )
    args = parser.parse_args()
    run(args)

