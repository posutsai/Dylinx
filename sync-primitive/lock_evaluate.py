
#!/usr/bin/env python3
import sys
import csv
import numpy as np
from scipy import stats
import math
import subprocess
import random
import matplotlib.pyplot as plt
from argparse import ArgumentParser
import re
import multiprocessing
from tqdm import tqdm
import numpy as np

BIN_LENGTH = 500000000 # ns
N_CPU_CORE = 70
C_EXECUTABLE = "a.out"

def post_analysis(record_path):
    intervals = {}
    with open(record_path) as df:
        for l in csv.reader(df, delimiter="\t"):
            if int(l[2]) not in intervals.keys():
                intervals[int(l[2])] = []
            intervals[int(l[2])].append({"entry": float(l[0]) * 1000, "exit": float(l[1]) * 1000})
    intervals = {b: sorted(intervals[b], key=lambda k: k['entry']) for b in intervals.keys()}
    # Note:
    # When the amount of samples is really low, there is possiblity that no thread get into
    # certain keys.
    start_time = min([ v[0]['entry'] for v in intervals.values()])
    end_time = max([ v[-1]['exit'] for v in intervals.values()])
    print("duration {} ns".format(end_time - start_time))
    cnt = 0
    pivots = {k: 0 for k in intervals.keys()}
    hist = []
    for bin, t in enumerate(np.arange(start_time, end_time, BIN_LENGTH)):
        cnt = 0
        for k in intervals.keys():
            if k != 0:
                continue
            rc = 0
            for i, op in enumerate(intervals[k][ pivots[k]: ]):
                if op['entry'] >= t + BIN_LENGTH:
                    pivots[k] += i
                    break
                # intersect with left bin boundary
                if t >= op['entry'] and t < op['exit']:
                    rc += 1
                    continue
                # intersect with right bin boundary
                if t + BIN_LENGTH > op['entry'] and op['exit'] > t + BIN_LENGTH:
                    rc += 1
                    continue
                if op['entry'] > t and op['exit'] < t + BIN_LENGTH:
                    rc += 1
                    continue
            # if rc > 1:
            #     cnt += 1
            cnt = rc
        hist.append(cnt)
    unique, count = np.unique(hist, return_counts=True)
    counts = dict(zip(unique, count))
    return counts

def get_poisson_lambda(counts):
    w_times = 0
    for e in counts.keys():
        w_times += e * counts[e]
    lda = w_times / sum(counts.values())
    return lda

def gen_dataset(n_cpu, hc_prob, executable_path, lock_type, wr_ratio, is_fix_cpu=False, cpus=None):
    lookup_table = {
        "none": 0,
        "mutex": 1,
        "rwlock": 2,
        "seqlock": 3,
        "rcu": 4,
    }
    if is_fix_cpu:
        cpu_arg = ','.join(map(str, cpus))
    else:
        perm = [i for i in range(N_CPU_CORE)]
        np.random.shuffle(perm)
        cpus = perm[:int(n_cpu)]
        cpu_arg = ','.join(map(str, cpus))
    # print("taskset -c {} ./{}".format(cpu_arg, executable_path), str(hc_prob), str(n_cpu), str(lookup_table[lock_type]), str(wr_ratio))
    with subprocess.Popen(args=["taskset", "-c", cpu_arg, "./{}".format(executable_path), str(hc_prob), str(n_cpu), str(lookup_table[lock_type]), str(wr_ratio)], stdout=subprocess.PIPE) as proc:
        return proc.stdout.read().decode('utf-8').split('\n')[0]

def uncontent_real_prob(counts):
    uc = 0
    uc = counts[0] + uc if 0 in counts.keys() else uc
    uc = counts[1] + uc if 1 in counts.keys() else uc
    return uc / sum(counts.values())

def lock_evaluate(num_core, evaluate_list=[("none"), ("mutex")], repeat_times=10):
    results = {}
    perm = [i for i in range(N_CPU_CORE)]
    np.random.shuffle(perm)
    cpus = perm[:int(num_core)]
    for lt in evaluate_list:
        print(lt)
        if "none" == lt:
            print("Processing without any lock ....")
            total = []
            for r in range(repeat_times):
                ldas = []
                for hc_prob in np.arange(0., 1.1, 0.1):
                    record_path =  gen_dataset(num_core, hc_prob, C_EXECUTABLE, "none", 0., is_fix_cpu=True, cpus=cpus)
                    print(record_path)
                    counts = post_analysis(record_path)
                    lda = get_poisson_lambda(counts) 
                    ldas.append(lda)
                    print(lda)
                total.append(ldas)
            mean = np.mean(total, axis=1)
            results["none"] = mean
            print(total)
        if "mutex" == lt:
            print("Evaluating mutex ....")
            total = []
            for r in range(repeat_times):
                ldas = []
                for hc_prob in np.arange(0., 1.1, 0.1):
                    record_path = gen_dataset(num_core, hc_prob, C_EXECUTABLE, "mutex", 0., is_fix_cpu=True, cpus=cpus)
                    counts = post_analysis(record_path)
                    lda = get_poisson_lambda(counts)
                    ldas.append(lda)
                    print(lda)
                total.append(ldas)
            mean = np.mean(total, axis=0)
            results["mutex"] = mean
            print(total)

        if "rwlock" == lt[0]:
            print("Evaluating rwlock ....")
            wr_ratio_start = 0.
            wr_ratio_end = 1.2
            wr_ratio_step = 0.2
            print("Start writer ratio from {} to {} with step {}".format(wr_ratio_start, wr_ratio_end, wr_ratio_step))
            for wr_ratio in np.arange(wr_ratio_start, wr_ratio_end, wr_ratio_step):
                ldas = []
                for hc_prob in np.arange(0., 1.1, 0.1):
                    record_path = gen_dataset(num_core, hc_prob, C_EXECUTABLE, "rwlock", wr_ratio, is_fix_cpu=True, cpus=cpus)
                    counts = post_analysis(record_path)
                    lda = get_poisson_lambda(counts)
                    ldas.append(lda / num_core)
                results["rwlock {}".format(wr_ratio)] = ldas

def process_args():
    parser = ArgumentParser()
    parser.add_argument("-c", "--core", dest="core", type=int, default=multiprocessing.cpu_count(),
                        help="Specify how many processors you would like to use.")
    args = parser.parse_args()
    lock_evaluate(args.core, repeat_times=1)

if __name__ == "__main__":
    process_args()
