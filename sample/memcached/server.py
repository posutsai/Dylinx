#!/usr/local/bin/python3
import sys
import argparse
import os
import subprocess
import asyncio
from aiohttp import web
import signal

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
        self.memcached_pid = -1

    async def execute_repo(self):
        os.environ["LD_LIBRARY_PATH"] = ":".join([
            f"{self.repo}/.dylinx/lib",
            f"{self.home_path}/build/lib"
        ])
        create = asyncio.create_subprocess_exec(
            f"{self.repo}/memcached-dlx", "-c", "100000", "-l", "0.0.0.0", "-t", "16", "-u", "root",
            stdout=asyncio.subprocess.PIPE
        )
        self.task = await create

    def stop_repo(self):
        self.task.send_signal(signal.SIGKILL)

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
    await subject.execute_repo()
    return web.json_response({
        "err": 0,
        "pid": subject.task.pid
    })

async def stop_subject(request):
    global subject
    subject.stop_repo()
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
