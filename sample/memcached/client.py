#!/usr/local/bin/python3
import sys
import requests
import argparse
import os
import subprocess
import time
import pickle
import operator
import json
import re
import numpy as np

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import ALLOWED_LOCK_TYPE

def is_float(string):
    try:
        float(string)
        return True
    except ValueError:
        return False

def parse_benchmark(log):
    pattern = re.compile(r'\[RUN #\d 100%, *\d* secs\].*\(avg: *(\d*)\) ops\/sec')
    op_per_sec = [ int(s) for s in re.findall(pattern, log)]
    return np.mean(op_per_sec), np.std(op_per_sec)

def deliver_comb(comb, ip):
    res = requests.post(
        f'http://{ip}:8787/set',
        json={ "slots": comb }
    )
    time.sleep(10)
    with subprocess.Popen(
        f"memtier_benchmark -s {ip} -p 11211 -c 50 -t 100 -n 2000 -x 5 --ratio=1:1 --pipeline=1 --key-pattern S:S -P memcache_binary --hide-histogram > log".split(' '),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    ) as benchmark:
        benchmark.wait()
        bm_err = benchmark.stderr.read().decode("utf-8")
    requests.get(f"http://{ip}:8787/stop")
    return bm_err

def run(args):
    res = requests.get(f'http://{args.ip}:8787/init')
    if res.ok:
        sites = res.json()["sites"]
    else:
        print("[ Error ] Client can't initialize dylinx monitoring subject")
    if args.mode == "same":
        combinations = { int(s): args.ltype for s in sites.keys() }
        recr = deliver_comb(combinations, args.ip)
        parse_benchmark(recr)
    if args.mode == "greedy":
        options = ["TTAS", "BACKOFF"]
        combinations = {s: args.ltype for s in sites.keys()}
        mean, std = parse_benchmark(deliver_comb(combinations, args.ip))
        best_setting = {}
        with open("record", 'w') as handler:
            handler.write(
                f"standard {args.ltype} MEAN: {mean}, STD: {std}\n"
                f"=============================================================\n"
            )
        for k in list(sites.keys()):
            op_per_sec = { o: 0. for o in options }
            for opt in options:
                combinations[k] = opt
                mean, std = parse_benchmark(deliver_comb(combinations, args.ip))
            # best_lock, amount = max(op_per_sec.items(), key=operator.itemgetter(1))
            # should keep the setting?
            # combinations[k] = best_lock
            # best_setting[k] = best_lock
                with open("record", "a") as handler:
                    handler.write(f"lock_id: {k}, type: {opt}, mean: {mean}, std: {std}\n")

        # aggregate_op = parse_benchmark(deliver_comb(best_setting, args.ip))
        # print(f"Combining op: {aggregate_op}")


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
        "-ip",
        type=str,
        required=True,
        help="Configuring the ip address"
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
