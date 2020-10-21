#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
from aiohttp import web

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import BaseSubject

class DylinxSubject(BaseSubject):
    def __init__(self, cc_path):
        super().__init__(cc_path)
        self.repo = os.path.abspath(os.path.dirname(self.cc_path))

    def build_repo(self, id2type):
        super().configure_type(id2type)
        print(os.environ["C_INCLUDE_PATH"])
        cmd = f"cd repo; make -f dylinx.mk memcached-dlx"
        with subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            shell=True
        ) as proc:
            print(proc.stdout.read().decode("utf-8"))

    async def execute_repo(self):
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{self.home_path}/build/lib"
        ])
        with subprocess.Popen(
            args=[f"{self.repo}/memcached-dlx", "-c", "100000", "-l", "0.0.0.0", "-t", "16", "-u", "root"],
            stdout=subprocess.PIPE
        ) as proc:
            self.memcached_pid = proc.pid
            return proc.stdout.read().decode("utf-8")

    def stop_repo(self):
        pass

async def init_subject(request):
    global subject
    subject = DylinxSubject("./repo/compiler_commands.json")
    return web.json_response({
        "err": 0,
        "sites": subject.get_pluggable()
    })

async def set_subject(request):
    global subject
    param = await request.json()
    subject.build_repo(param["slots"])
    log = subject.execute_repo()
    return web.json_response({
        "err": 0,
        "log": 11
    })

if __name__ == "__main__":
    app = web.Application()
    app.add_routes([
        web.get('/init', init_subject),
        web.post('/set', set_subject),
    ])
    web.run_app(app, host="0.0.0.0", port=8787)
