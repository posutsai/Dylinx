#!/usr/local/bin/python3
import sys
import glob
import os
import subprocess
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

    def stop_repo(self):
        return

    def make_report(self):
        self.runtime_report = DylinxRuntimeReport("/tmp/xray-colony-inspect.yaml")
        self.runtime_report.plot_lifetime_superposition_graph("./test.png")

if __name__ == "__main__":
    with ColonySubject("./compile_commands.json") as subject:
        sites = subject.get_pluggable()
        sorted_site_ids = sorted(sites.keys())
        id2type = {k: "PTHREADMTX" for k in sorted_site_ids}
        subject.build_repo(id2type)
        subject.execute_repo()
        subject.make_report()
