#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
from subprocess import PIPE

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject, ALLOWED_LOCK_TYPE

class DylinxSubject(BaseSubject):
    def __init__(self, cc_path):
        super().__init__(cc_path)
        self.repo = os.path.abspath(os.path.dirname(self.cc_path))

    def build_repo(self, id2type):
        super().configure_type(id2type)
        cmd = f"cd repo; make -f dylinx.mk python3-dlx"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, shell=True)
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self):
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        with subprocess.Popen(f"pyperformance run --python=python3-dlx -o py-dlx.json".split(' '), stdout=PIPE, stderr=PIPE) as task:
            print(task.stdout.read().decode("utf-8"))
            print(task.stderr.read().decode("utf-8"))

    def stop_repo(self):
        return

def run(args):
    subject = DylinxSubject("./repo/compiler_commands.json")
    sites = subject.get_pluggable()
    print(sites)
    if args.mode == "same":
        id2type = { s: args.ltype for s in sites.keys() }
        print(id2type)
        subject.build_repo(id2type)


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

