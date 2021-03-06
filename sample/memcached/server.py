#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
import asyncio
from aiohttp import web
import signal
import time
import socket
import multiprocessing

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject

class DylinxSubject(BaseSubject):
    def __init__(self, cc_path):
        super().__init__(cc_path)
        self.repo = os.path.abspath(os.path.dirname(self.cc_path))
        self.xray_options = "patch_premain=true xray_mode=xray-basic xray_logfile_base=xray-log/ "
        self.basic_options = "func_duration_threshold_us=0"

    def build_repo(self, id2type):
        super().configure_type(id2type)
        cmd = f"cd repo; make -f dylinx.mk memcached-dlx"
        proc = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.DEVNULL, shell=True)
        err_msg = proc.stderr.read().decode("utf-8")
        if len(err_msg) != 0:
            print(err_msg)

    def execute_repo(self, active_xray):
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{os.environ['DYLINX_HOME']}/build/lib"
        ])
        if not active_xray:
            self.task = subprocess.Popen(
                [f"{self.repo}/memcached-dlx", "-c", "100000", "-l", "0.0.0.0", "-t", f"{multiprocessing.cpu_count()}"],
                stdout=subprocess.DEVNULL
            )
        else:
            os.environ["XRAY_OPTIONS"] = self.xray_options
            os.environ["XRAY_BASIC_OPTIONS"] = self.basic_options
            print(os.environ["XRAY_OPTIONS"])
            print(os.environ["XRAY_BASIC_OPTIONS"])
            self.task = subprocess.Popen(
                [f"{self.repo}/memcached-dlx", "-c", "100000", "-l", "0.0.0.0", "-t", f"{multiprocessing.cpu_count()}"],
                stdout=subprocess.DEVNULL, env=os.environ
            )
        time.sleep(3)

    def stop_repo(self):
        self.task.kill()

async def init_subject(request):
    global subject
    subject = DylinxSubject("./repo/compiler_commands.json")
    return web.json_response({
        "err": 0,
        "sites": subject.get_pluggable(),
        "hostname": socket.gethostname(),
    })

async def set_subject(request):
    global subject
    print("set memcached...")
    param = await request.json()
    subject.build_repo(param["slots"])
    subject.execute_repo(param["active_xray"])
    return web.json_response({
        "err": 0,
    })

async def stop_subject(request):
    global subject
    subject.stop_repo()
    print("stop memcached...")
    return web.json_response({
        "err": 0
    })

if __name__ == "__main__":
    app = web.Application()
    app.add_routes([
        web.get('/init', init_subject),
        web.post('/set', set_subject),
        web.get('/stop', stop_subject),
    ])
    web.run_app(app, host="0.0.0.0", port=8787)
