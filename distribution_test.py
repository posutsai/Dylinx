#!/usr/bin/env python3
import sys
import csv
import numpy as np
from scipy import stats
import math

N_BARROW = 16
BIN_LENGTH = 100000 # ms



def bhattacharyya(a, b):
    """ Bhattacharyya distance between distributions (lists of floats). """
    if not len(a) == len(b):
        raise ValueError("a and b must be of the same size")
    return -math.log(sum((math.sqrt(u * w) for u, w in zip(a, b))))

intervals = {i: [] for i in range(N_BARROW)}
with open("records/abc.tsv") as df:
    for l in csv.reader(df, delimiter="\t"):
        intervals[int(l[2])].append({"entry": int(l[0]), "exit": int(l[1])})
intervals = {i: sorted(intervals[i], key=lambda k: k['entry']) for i in range(N_BARROW)}
# Note:
# When the amount of samples is really low, there is possiblity that no thread get into
# certain keys.
start_time = min([ intervals[i][0]['entry'] for i in range(N_BARROW)])
end_time = max([ intervals[i][-1]['exit'] for i in range(N_BARROW)])
cnt = 0

pivots = {i: 0 for i in range(N_BARROW)}
hist = []
for bin, t in enumerate(range(start_time, end_time, BIN_LENGTH)):
    cnt = 0
    for k in range(N_BARROW):
        for i, op in enumerate(intervals[k][ pivots[k]: ]):
            if op['entry'] >= t + BIN_LENGTH:
                pivots[k] += i
                break
            # intersect with left bin boundary
            if t >= op['entry'] and t < op['exit']:
                cnt += 1
                continue
            # intersect with right bin boundary
            if t + BIN_LENGTH > op['entry'] and op['exit'] > t + BIN_LENGTH:
                cnt += 1
                continue
            if op['entry'] > t and op['exit'] < t + BIN_LENGTH:
                cnt += 1
                continue
    hist.append(cnt)
unique, count = np.unique(hist, return_counts=True)
counts = dict(zip(unique, count))
real_prob_dist = []
for i in range(max(unique) + 1):
    if i in counts.keys():
        real_prob_dist.append(counts[i] / sum(count))
    else:
        real_prob_dist.append(0)
print(real_prob_dist)
w_times = 0
for e in range(len(unique)):
    w_times += unique[e] * count[e]
lda = w_times / sum(count)
print(f"lambda is {lda}")
poisson_prob_dist = [stats.poisson.pmf(i, lda)for i in range(max(unique) + 1)]
binomial_prob_dist = [stats.binom.pmf(i, 40, 1/N_BARROW) for i in range(max(unique) + 1)]
print(poisson_prob_dist)
print(bhattacharyya(real_prob_dist, poisson_prob_dist))
print(bhattacharyya(real_prob_dist, binomial_prob_dist))
