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
import pickle

N_CPU_CORE = 70
C_EXECUTABLE = "a.out"

# Since we have to make the lock tested as much as possible, there is no need to set bin.
def post_analysis(record_path):
    records = []
    with open(record_path) as df:
        for l in csv.reader(df, delimiter='\t'):
            records.append({ # record unit ns
                "acquiring_ts": float(l[0]),
                "holding_ts": float(l[1]),
                "releasing_ts": float(l[2]),
            })
    records = sorted(records, key=lambda k: k['holding_ts'])

    execution_time = []
    total = {c: {"occurrence":0, "contention_time": []} for c in range(N_CPU_CORE)}
    # Calculate for each holding moment
    for i, h in enumerate(records):
        execution_time.append(h["releasing_ts"] - h["holding_ts"])
        cnt = 0
        # Count number of threads that are contending for the same lock
        for r in records:
            if r["acquiring_ts"] < h["holding_ts"] and r["holding_ts"] > h["releasing_ts"]:
                cnt += 1
        if i > 0 and cnt > 0:
            print(h)
            print(records[i-1])
            print(i, cnt)
            print(h["holding_ts"] - records[i - 1]["releasing_ts"])
            assert(h["holding_ts"] > records[i - 1]["releasing_ts"])
        total[cnt]["occurrence"] += 1
    print("execution time : mean {}ns, std {}ns".format(
        np.mean(execution_time),
        np.std(execution_time))
    )
    for i in range(4):
        print("occurrence {} overhead mean {} ns std {} ns".format(
            total[i]["occurrence"],
            np.mean(total[i]["contention_time"]),
            np.std(total[i]["contention_time"])
        ))

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

def lock_evaluate(num_core, evaluate_list=["none", "mutex", "rwlock"], repeat_times=10):
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
                    counts = post_analysis(record_path)
                    lda = get_poisson_lambda(counts) 
                    ldas.append(lda)
                total.append(ldas)
            print(total)
            mean = np.mean(total, axis=0)
            results["none"] = mean
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
                total.append(ldas)
            print(total)
            mean = np.mean(total, axis=0)
            results["mutex"] = mean

        if "rwlock" == lt:
            print("Evaluating rwlock ....")
            wr_ratio_start = 0.
            wr_ratio_end = 1.2
            wr_ratio_step = 0.2
            print("Start writer ratio from {} to {} with step {}".format(wr_ratio_start, wr_ratio_end, wr_ratio_step))
            for wr_ratio in np.arange(wr_ratio_start, wr_ratio_end, wr_ratio_step):
                total = []
                for r in range(repeat_times):
                    ldas = []
                    for hc_prob in np.arange(0., 1.1, 0.1):
                        record_path = gen_dataset(num_core, hc_prob, C_EXECUTABLE, "rwlock", wr_ratio, is_fix_cpu=True, cpus=cpus)
                        counts = post_analysis(record_path)
                        lda = get_poisson_lambda(counts)
                        ldas.append(lda)
                    total.append(ldas)
                mean = np.mean(total, axis=0)
                results["rwlock_{:2.2f}".format(wr_ratio)] = mean
    with open("result.pkle", 'wb') as f:
        pickle.dump(results, f)
    return results

def plot_evaluation(result, cores):
    fig, ax = plt.subplots()
    plt.title("Synchronization Primitive Evaluation")
    plt.ylabel("lambda, equivalent thread number")
    plt.xlabel("race condition rate")
    rc_rate = list([rc for rc in np.arange(0., 1.1, 0.1)])
    theo_proc = list([rc * cores for rc in np.arange(0., 1.1, 0.1)])
    ax.plot(rc_rate, theo_proc, label="theoretical core")
    for k in result.keys():
        l, = ax.plot(rc_rate, result[k], label=k)
    ax.legend()
    plt.savefig("sync_primitive_eva.png")

def process_args():
    parser = ArgumentParser()
    parser.add_argument("-c", "--core", dest="core", type=int, default=multiprocessing.cpu_count(),
                        help="Specify how many processors you would like to use.")
    args = parser.parse_args()
    result = lock_evaluate(args.core, evaluate_list=["none", "mutex", "rwlock"], repeat_times=20)
    plot_evaluation(result, args.core)

if __name__ == "__main__":
    post_analysis("records/mutex_TAS/0.500_5_v3.tsv")
