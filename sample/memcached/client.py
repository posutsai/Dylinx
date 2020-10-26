#!/usr/local/bin/python3
import sys
import requests
import argparse
import os
import subprocess
import time
import pickle
import operator
if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import ALLOWED_LOCK_TYPE

def is_float(string):
    try:
        float(string)
        return True
    except ValueError:
        return False

def parse_benchmark(stdout):
    gets_msg = stdout.split('\n')[10]
    gets = float(list(filter(lambda e: is_float(e), gets_msg.split(' ')))[1])
    sets_msg = stdout.split('\n')[9]
    sets = float(list(filter(lambda e: is_float(e), sets_msg.split(' ')))[1])
    return gets + sets

def deliver_comb(comb):
    res = requests.post(
        f'{os.environ["DYLINX_DOMAIN"]}:8787/set',
        json={ "slots": comb }
    )
    time.sleep(3)
    with subprocess.Popen(
        f"memtier_benchmark -s {args.ip} -p 11211 -c 5 -t 20 -n 20 -d 100 --ratio=1:1 --pipeline=1 --key-pattern S:S -P memcache_binary --hide-histogram > log".split(' '),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    ) as benchmark:
        benchmark.wait()
        bm_log = benchmark.stdout.read().decode("utf-8")
    requests.get(f"{os.environ['DYLINX_DOMAIN']}:8787/stop")
    return bm_log

def run(args):
    res = requests.get(f'{os.environ["DYLINX_DOMAIN"]}:8787/init')
    if res.ok:
        sites = res.json()["sites"]
    else:
        print("[ Error ] Client can't initialize dylinx monitoring subject")
    if args.mode == "same":
        recr = deliver_comb({ int(s): args.ltype for s in sites.keys() })
        print(recr)
    if args.mode == "greedy":
        options = ["PTHREADMTX", "TTAS", "BACKOFF"]
        combinations = {s: args.ltype for s in sites.keys()}
        for k in sites.keys():
            op_per_sec = { o: 0. for o in options }
            for opt in options:
                combinations[k] = opt
                recr = deliver_comb(combinations)
                op_per_sec[opt] = parse_benchmark(recr)
            best_lock = max(op_per_sec.items(), key=operator.itemgetter(1))[0]
            combinations[k] = best_lock

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
