#!/usr/bin/env python3
import sys
import csv
import numpy as np
from scipy import stats
import math
import subprocess
import random
import matplotlib.pyplot as plt

BIN_LENGTH = 50000 # ms
N_CPU_CORE = 40

def bhattacharyya(a, b):
    """ Bhattacharyya distance between distributions (lists of floats). """
    if not len(a) == len(b):
        raise ValueError("a and b must be of the same size")
    return -math.log(sum((math.sqrt(u * w) for u, w in zip(a, b))))

def post_analysis(record_path, n_barrows):
    intervals = {i: [] for i in range(n_barrows)}
    with open(record_path) as df:
        for l in csv.reader(df, delimiter="\t"):
            intervals[int(l[2])].append({"entry": float(l[0]), "exit": float(l[1])})
    intervals = {i: filter(lambda intv: intv["exit"] > intv["entry"], intervals[i]) for i in range(n_barrows)}
    intervals = {i: sorted(intervals[i], key=lambda k: k['entry']) for i in range(n_barrows)}
    # Note:
    # When the amount of samples is really low, there is possiblity that no thread get into
    # certain keys.
    start_time = int(min([ intervals[i][0]['entry'] for i in range(n_barrows)]))
    end_time = int(max([ intervals[i][-1]['exit'] for i in range(n_barrows)]))
    cnt = 0
    pivots = {i: 0 for i in range(n_barrows)}
    hist = []
    for bin, t in enumerate(range(start_time, end_time, BIN_LENGTH)):
        cnt = 0
        for k in range(n_barrows):
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
            if rc > 1:
                cnt += 1
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


def uncontent_real_prob(counts):
    return counts[0] / sum(counts.values())

def gen_dataset(n_cpu, n_barrows, op_length, executable_path):
    perm = [i for i in range(N_CPU_CORE)]
    np.random.shuffle(perm)
    cpus = perm[:n_cpu]
    cpu_arg = ','.join(map(str, cpus))
    output = subprocess.run(args=["taskset", f"{cpu_arg}", f"./{executable_path}", str(n_barrows), str(op_length), str(n_cpu)],
                             capture_output=True, text=True)
    return output.stdout.split('\n')[0]

def poisson_vs_binomial():
    hash_prob = []
    poisson = []
    binom = []
    real_prob = []
    for i in range(2, 32, 1):
        record_path = gen_dataset(4, i, 16384, "a.out")
        counts = post_analysis(record_path, i)
        lda = get_poisson_lambda(counts)
        print(lda, stats.poisson.pmf(0, lda))
        hash_prob.append(i)
        poisson.append(stats.poisson.pmf(0, lda))
        binom.append(stats.binom.pmf(0, 4, 1 / i) + stats.binom.pmf(1, 4, 1 / i))
        real_prob.append(uncontent_real_prob(counts))
    fig, ax = plt.subplots()
    plt.title("Poisson descripe Race Condition better")
    plt.ylabel("uncontention prob")
    plt.xlabel("barrow number")
    l1, = ax.plot(hash_prob, real_prob, label='real probability')
    l1.set_dashes([1, 2, 1, 2])
    l2, = ax.plot(hash_prob, poisson, label='poisson')
    l2.set_dashes([2, 2, 10, 2])
    l3, = ax.plot(hash_prob, binom, label='binom')
    l3.set_dashes([5, 2, 5, 2])
    ax.legend()
    plt.savefig("poisson_vs_binomial.png")


if __name__ == '__main__':
    poisson_vs_binomial()
