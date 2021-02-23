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
import pickle
import numpy as np
import matplotlib.pyplot as plt
import itertools

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
    time.sleep(5)
    with subprocess.Popen(
        f"memtier_benchmark -s {ip} -p 11211 -c 50 -t 100 -n 2000 -x 5 --ratio=1:1 --pipeline=1 --key-pattern S:S -P memcache_binary --hide-histogram > log".split(' '),
        stdout=subprocess.PIPE, stderr=subprocess.PIPE
    ) as benchmark:
        benchmark.wait()
        bm_err = benchmark.stderr.read().decode("utf-8")
    requests.get(f"http://{ip}:8787/stop")
    return bm_err

def draw_perf_gain(log_path, fig_path, server_name):
    with open(log_path, "rb") as handler:
        log = pickle.load(handler)
    round2mutate = {v[3]: (k, v[0], v[1], v[2]) for k, v in log["opt_process"].items()}
    fig, ax = plt.subplots()
    ax.set_title(f"Performance gain after n round of opt [{server_name}]")
    base_mean = log["wo_opt"][0]
    rounds = [k + 1 for k in log["opt_process"].keys()]
    rounds = np.array((range(len(log["opt_process"].keys())))) + 1
    perf_gains = np.array([((round2mutate[r - 1][2] / base_mean) - 1) * 100 for r in rounds])
    print(perf_gains)
    perf_upper = np.array([(((round2mutate[r - 1][2] + round2mutate[r - 1][3]) / base_mean) - 1) * 100 for r in rounds])
    perf_lower = np.array([(((round2mutate[r - 1][2] - round2mutate[r - 1][3]) / base_mean) - 1) * 100 for r in rounds])
    for i, pg in enumerate(perf_gains):
        ax.annotate(f"{round2mutate[i][0]}", (i+1, pg))
    litl_pg = (log["litl_best"][1] / base_mean - 1) * 100
    perf_gains = np.insert(perf_gains, 0, 0)
    perf_upper = np.insert(perf_upper, 0, 0)
    perf_lower = np.insert(perf_lower, 0, 0)
    pad_rounds = np.insert(rounds, 0, 0)
    ax.plot(pad_rounds, perf_gains, label="Dylinx", marker=",", zorder=1)
    ax.fill_between(pad_rounds, perf_upper, perf_lower, alpha=0.15)
    ax.plot(
        pad_rounds,
        [litl_pg if r != 0 else 0 for r in pad_rounds],
        "--",
        label=f"LiTL({log['litl_best'][0]})"
    )
    for lt in ["PTHREADMTX", "BACKOFF", "TTAS", "ADAPTIVEMTX"]:
        filter_r = np.array(list(filter(lambda r: round2mutate[r - 1][1] == lt, rounds)))
        if len(filter_r) > 0:
            ax.scatter(filter_r, perf_gains[filter_r], label=lt, marker="v", zorder=2)
    plt.xlabel("Optimization round")
    plt.ylabel("Performance gain")
    ax.legend()
    plt.savefig(fig_path)

def run(args):
    res = requests.get(f'http://{args.ip}:8787/init')
    if res.ok:
        sites = {int(k): v for k, v in res.json()["sites"].items()}
        server_name = {
            "merry": "Small",
            "sunny": "Big"
        }[res.json()["hostname"]]
    else:
        print("[ Error ] Client can't initialize dylinx monitoring subject")
    if args.mode == "litl":
        options = ["TTAS", "BACKOFF", "ADAPTIVEMTX", "PTHREADMTX"]
        for o in options:
            combinations = { int(s): o for s in sites.keys() }
            recr = deliver_comb(combinations, args.ip)
            mean, std = parse_benchmark(recr)
            print(f"litl mode [{o}] mean = {mean}, std = {std}")
    elif args.mode == "assign":
        combinations = {int(s): "PTHREADMTX" for s in sites.keys()}
        combinations[5] = "TTAS"
        combinations[16] = "TTAS"
        combinations[19] = "TTAS"
        # combinations[20] = "TTAS"
        # combinations[21] = "TTAS"
        # combinations[33] = "BACKOFF"
        # combinations[42] = "TTAS"
        # combinations[49] = "BACKOFF"
        print(combinations)
        mean, std = parse_benchmark(deliver_comb(combinations, args.ip))
        print(f"mean = {mean}, std = {std}")
    elif args.mode == "greedy":
        options = ["PTHREADMTX", "BACKOFF", "TTAS", "ADAPTIVEMTX"]
        n_round = 10
        combinations = {s: "PTHREADMTX" for s in sites.keys()}
        print(f"There are total {len(sites.keys())} sites.")
        litl_recr = {}
        for o in options:
            combinations = { int(s): o for s in sites.keys() }
            recr = deliver_comb(combinations, args.ip)
            mean, std = parse_benchmark(recr)
            litl_recr[o] = (mean, std)
            print(f"litl mode [{o}] mean = {mean}, std = {std}")
        litl_rank = sorted(options, key=lambda k: litl_recr[k][0])
        litl_best = litl_rank[-1]
        # litl_best = "PTHREADMTX"
        litl_2nd = "TTAS"
        if litl_best == "PTHREADMTX":
            print("Best type found by LiTL is the original type")
            combination = { int(s): "PTHREADMTX" for s in sites.keys() }
            site_trials = {}
            for i, s in enumerate(sites.keys()):
                combination[s] = litl_2nd
                print(combination)
                mean, std = parse_benchmark(deliver_comb(combination, args.ip))
                site_trials[s] = {
                    "testing": litl_2nd,
                    "mean": mean,
                    "std": std,
                }
                print(f"#{i} site {s:2d} mean = {mean:.2f}, std = {std:.2f}")
                combination[s] = "PTHREADMTX"
            critical_rank = sorted(sites.keys(), key=lambda s: -1 * (site_trials[s]["mean"] - litl_recr[litl_best][0]) ** 2)
            candidate = critical_rank[:5]
            print(f"get candidate {candidate}")
            search_space = list(itertools.product(*([options] * 5)))
            comb_trials = {}
            for comb in search_space:
                combinations = {s: "PTHREADMTX" for s in sites.keys()}
                for i, t in enumerate(comb):
                    combinations[candidate[i]] = t
                mean, std = parse_benchmark(deliver_comb(combination, args.ip))
                comb_trials[comb] = (mean, std)
                print(f"{comb} mean = {mean}, std = {std}")

            with open("result/site_trial.pkl", "wb") as handler:
                pickle.dump({
                    "wo_opt": litl_recr["PTHREADMTX"],
                    "litl_recr": litl_recr,
                    "critical_site": candidate,
                    "search_space": comb_trials
                }, handler)
            for r, c in enumerate(sorted(search_space, key=lambda n: -1 * comb_trials[n][0])[:10]):
                mean, std = comb_trials[c]
                print(f"#{r:2d} {c} mean = {mean:.2f}, std = {std:.2f}")

        else:
            base_mean, base_std = litl_recr["PTHREADMTX"]
            print(f"standard (all pthreadmtx) mean = {base_mean:.2f}, std = {base_std:.2f}")
            remain_keys = list(sites.keys())
            critical_sites = {}
            current_best_recr = (base_mean, base_std)
            for n in range(n_round):
                current_best_comb = {s: critical_sites.get(s, ("PTHREADMTX", None, None, None))[0] for s in sites.keys()}
                site_trials = {}
                for k in remain_keys:
                    # Search for most critical site
                    current_best_comb[k] = litl_best
                    print(current_best_comb)
                    mean, std = parse_benchmark(deliver_comb(current_best_comb, args.ip))
                    site_trials[k] = (mean, std)
                    current_best_comb[k] = "PTHREADMTX"
                site_ranks = sorted(remain_keys, key=lambda k: site_trials[k][0] - current_best_recr[0])
                best_site = site_ranks[-1]
                need2try = options.copy()
                need2try.remove(litl_best)
                need2try.remove("PTHREADMTX")
                type_trials = {
                    litl_best: site_trials[best_site],
                    "PTHREADMTX": current_best_recr
                }
                for t in need2try:
                    current_best_comb[best_site] = t
                    mean, std = parse_benchmark(deliver_comb(current_best_comb, args.ip))
                    type_trials[t] = (mean, std)
                    current_best_comb[best_site] = "PTHREADMTX"
                type_ranks = sorted(options, key=lambda k: type_trials[k])
                best_type = type_ranks[-1]
                critical_sites[best_site] = (best_type, *type_trials[best_type], n)
                current_best_recr = type_trials[best_type]
                print(f"round_{n:02d} locate site = {best_site}, type = {best_type}, recr = {type_trials[best_type]}")
                remain_keys.remove(best_site)
                with open("log.pkl", "wb") as handler:
                    pickle.dump(
                        {
                            "wo_opt": (base_mean, base_std),
                            "opt_process": critical_sites,
                            "litl_best": (litl_best, *litl_recr[litl_best]),
                            "server_hostname": server_name
                        },
                        handler
                    )
                draw_perf_gain("log.pkl", f"result/opt_step_{server_name}.png", server_name)

if __name__ == "__main__":
    # draw_perf_gain("log.pkl", f"result/opt_step_test.png", "test")
    # sys.exit()
    parser = argparse.ArgumentParser(formatter_class=argparse.RawTextHelpFormatter)
    parser.add_argument(
        "-m",
        "--mode",
        type=str,
        choices=["litl", "greedy", "assign"],
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
