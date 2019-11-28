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

BIN_LENGTH = 5 # ms
N_CPU_CORE = 40
C_EXECUTABLE = "a.out"

def bhattacharyya(a, b):
    """ Bhattacharyya distance between distributions (lists of floats). """
    if not len(a) == len(b):
        raise ValueError("a and b must be of the same size")
    return -math.log(sum((math.sqrt(u * w) for u, w in zip(a, b))))

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


def uncontent_real_prob(counts):
    return (counts[0] + counts[1]) / sum(counts.values())

def gen_dataset(n_cpu, hc_prob, op_length, n_barrow, executable_path):
    perm = [i for i in range(N_CPU_CORE)]
    np.random.shuffle(perm)
    cpus = perm[:n_cpu]
    cpu_arg = ','.join(map(str, cpus))
    with subprocess.Popen(args=["taskset", "-c", f"{cpu_arg}", f"./{executable_path}", str(hc_prob), str(op_length), str(n_cpu), str(n_barrow)], stdout=subprocess.PIPE) as proc:
        return proc.stdout.read().decode('utf-8').split('\n')[0]

def poisson_vs_binomial(n_cpu, n_barrow, op_len, start, end, step):
    hash_prob = []
    poisson = []
    binom = []
    real_prob = []
    for i in np.arange(start, end, step):
        record_path = gen_dataset(3, i, op_len, C_EXECUTABLE)
        counts = post_analysis(record_path)
        lda = get_poisson_lambda(counts)
        print("rc_rate: {:.2f} lambda: {:.2f} uncontention_prob: {:.2f}".format(i, lda, stats.poisson.pmf(0, lda) + stats.poisson.pmf(1, lda)))
        hash_prob.append(i)
        poisson.append(stats.poisson.pmf(0, lda) + stats.poisson.pmf(1, lda))
        binom.append(stats.binom.pmf(0, 5, i) + stats.binom.pmf(1, 4, i))
        real_prob.append(uncontent_real_prob(counts))
    fig, ax = plt.subplots()
    plt.title("Poisson describe Race Condition better")
    plt.ylabel("uncontention prob")
    plt.xlabel("hash collision ratio")
    l1, = ax.plot(hash_prob, real_prob, label='real probability')
    l1.set_dashes([1, 2, 1, 2])
    l2, = ax.plot(hash_prob, poisson, label='poisson')
    l2.set_dashes([2, 2, 10, 2])
    l3, = ax.plot(hash_prob, binom, label='binom')
    l3.set_dashes([5, 2, 5, 2])
    ax.legend()
    plt.savefig("poisson_vs_binomial.png")

def bucket_core_ratio_vs_lambda(cores, op_length, start, end, num):
    # Intuitive experiment design is to adjust the number of  processors slightly
    # bit by bit. However, after the second thought, fixing the core number and tune
    # the ratio between cores and barrows may work as well.
    results = {}
    for r in tqdm(np.geomspace(start, end, num)):
        n_barrow = int(r * cores)
        ldas = []
        for hc_prob in np.arange(0., 1., 0.05):
            record_path = gen_dataset(cores, hc_prob, op_length, n_barrow, C_EXECUTABLE)
            counts = post_analysis(record_path)
            ldas.append(get_poisson_lambda(counts))
        results[r] = ldas
    fig, ax = plt.subplots()
    plt.title("The effect of lambda caused by barrow / cpu ratio")
    plt.ylabel("lambda")
    plt.xlabel("hash collision prob")
    for r in results.keys():
        l = ax.plot(np.arange(0., 1., 0.05), results[r], label="r={:.2f}".format(r))
    ax.legend()
    plt.savefig("BCratio_vs_lambda.png")

def process_arg():
    parser = ArgumentParser()
    parser.add_argument("var_mode", type=str, help= \
                        """
                        Specify which variavle would be iterate through range. It is a required argument with limited
                        option.
                        """)
    parser.add_argument("range", type=str, help= \
                        """
                        The range argument is used to extract a specific range depending on each mode.
                        """)
    parser.add_argument("-c", "--core", dest="core", type=int, default=multiprocessing.cpu_count(),
                        help="Specify how many processors you would like to use.")
    parser.add_argument("-hcp", "--hash_col_prob", dest="hc_prob", type=float, default=0.4,
                        help="The probability that two thread may collide to each other.")
    parser.add_argument("-l", "--op_len", dest="op_len", type=int, default=2**20,
                        help="The amount of loop iteration in workload of each thread.")
    parser.add_argument("-bcr", "--buck_core_ratio", dest="bc_ratio", type=float, default=-1.,
                        help="The ratio between bucket and core number.")
    args = parser.parse_args()

    total_core=multiprocessing.cpu_count()
    try:
        assert (args.var_mode in ["UcRate2HashProb", "CoreBuckRatio2Lda"]), \
        """
        var_mode argument should be in one of these mode. [\"rc_prob\"]
        """
        if args.var_mode == "UcRate2HashProb":
            if args.bc_ratio == -1:
                args.bc_ratio = 30.
            assert (args.core and args.op_len), "core and op_len arguments are required in rc_prob mode"
            assert (int(args.bc_ratio * args.core) > 0), "Either bc_ratio or core is illegal."
            pat = re.compile("[-+]?([0-9]*\.[0-9]+|[0-9]+)~[-+]?([0-9]*\.[0-9]+|[0-9]+),[-+]?([0-9]*\.[0-9]+|[0-9]+)")
            m = pat.match(args.range)
            assert (m and len(m.groups()) == 3), f"The input string is not compatable to the pattern: {args.range}"
            start, end, step = map(lambda g: float(g), m.groups())
            assert (start <= 1. and end <= 1. and step <= 1.), \
            f"""
            One or even more extracted arguments within start={start}, end={end} and step={end} are illegal.
            """
            print(f"Start ploting uncontention rate - hash collision ratio graph ...")
            print(f"The collision rate will start from {start} and end to {end} with step {step}")
            poisson_vs_binomial(args.core, int(args.bc_ratio * args.core), args.op_len, start, end, step)
        elif args.var_mode == "CoreBuckRatio2Lda":
            pat = re.compile("[-+]?([0-9]*\.[0-9]+|[0-9]+)~[-+]?([0-9]*\.[0-9]+|[0-9]+),[-+]?([0-9]*\.[0-9]+|[0-9]+)")
            m = pat.match(args.range)
            start, end, num = map(lambda g: float(g), m.groups())
            assert(num > 2), "Supposed to have more than two numbers in geometric space"
            bucket_core_ratio_vs_lambda(args.core, args.op_len, start, end, num)
        else:
            assert (False), \
            """
            The var_mode is legal. However, somehow the system doesn't match any of them. Please add an extra \"elif\"
            block if you would like to create a new mode.
            """
    except AssertionError as e:
        print(e)
        sys.exit(-1)


if __name__ == '__main__':
    process_arg()
