#!/usr/local/bin/python3
import sys
import requests
import argparse
import os
if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import ALLOWED_LOCK_TYPE

def run(args):
    res = requests.get(f'{os.environ["DYLINX_DOMAIN"]}:8787/init')
    if res.ok:
        sites = res.json()["sites"]
    else:
        print("[ Error ] Client can't initialize dylinx monitoring subject")
    if args.mode == "same":
        print(sites.keys())
        res = requests.post(
            f'{os.environ["DYLINX_DOMAIN"]}:8787/set',
            json={ "slots": { int(s): args.ltype for s in sites.keys() } }
        )
        print(res.json())
        res = requests.get(f"{os.environ['DYLINX_DOMAIN']}:8787/stop")


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
        choices=ALLOWED_LOCK_TYPE,
        help="Configure lock type."
    )
    args = parser.parse_args()
    run(args)
