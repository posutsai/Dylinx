#!/usr/local/bin/python3
from math import exp
import numpy as np
from scipy.special import comb
from scipy.special import factorial

def solve_lock_overhead(delta, q_model, measured_resp):
    eq_cs = q_model.critical_time + delta
    return compute_response_time(eq_cs, q_model.parallel_time, q_model.n_core) - measured_resp

def compute_response_time(critical_time, parallel_time, n_core):
    # s_func is the Laplace-Stieltjes Transform of
    # dirac delta function.
    def s_func(x):
        return x * exp(-1 * x * critical_time)

    def b_series(n, alpha):
        assert(n < n_core)
        if n == 0:
            return 1
        else:
            pi = np.float128(1.)
            for i in range(1, n + 1):
                pi = pi * (1 - s_func(i * alpha)) / s_func(i * alpha)
            return pi

    sigma = np.float128(0.)
    alpha = 1. / parallel_time
    for n in range(n_core):
        sigma += comb(n_core - 1, n) * b_series(n, alpha)
    p0 = 1. / (1 + n_core * critical_time  * sigma / parallel_time)
    a = 1 - p0
    exp_factor = a / critical_time
    return n_core / exp_factor - parallel_time

class MachineRepairGeneralQueue:
    # Response_time = waiting_time + repairing_time
    def __init__(self, critical_time, ratio, n_core):
        self.critical_time = critical_time
        self.parallel_time = critical_time / ratio
        self.n_core = n_core
        self.alpha = 1. / self.parallel_time
    def lockless_response_time(self):
        return compute_response_time(self.critical_time, self.parallel_time, self.n_core)


class RouteQueue:
    def __init__(self, duration):
        self.duration = duration
        self.serv_rate = 1. / self.duration

class RouteInfiniteQueue(RouteQueue):
    def __init__(self, duration):
        self.duration = duration
        self.serv_rate = float("inf")

class QNetwork:
    def __init__(self, queues, route_mtx):
        self.route_mtx = np.array(route_mtx)
        self.lut = {i: {"Waiting": {}, "Length": {}} for i in range(len(queues)) }
        self.lut["Throughput"] = {}
        w_len, h_len = self.route_mtx.shape
        assert(w_len == h_len)
        assert(len(queues) == w_len)
        self.queues = queues
        self.compute_visit_ratio()

    def compute_visit_ratio(self):
        w_len, h_len = self.route_mtx.shape
        a = np.append(np.transpose(self.route_mtx)-np.identity(w_len),[np.ones(w_len).tolist()],axis=0)
        b = np.zeros(w_len + 1)
        b[-1] = 1
        self.visit_ratio = np.linalg.solve(np.transpose(a).dot(a), np.transpose(a).dot(b))

    def waiting_time(self, qid, member):
        assert(member > 0)
        q = self.queues[qid]
        if type(q) == "InfiniteQueue":
            return q.duration
        elif member in self.lut[qid]["Waiting"].keys():
            return self.lut[qid]["Waiting"][member]
        else:
            output = (1 + self.queue_length(qid, member - 1)) / q.serv_rate
            self.lut[qid]["Waiting"][member] = output
            return output

    def queue_length(self, qid, member):
        if member == 0:
            return 0
        elif member in self.lut[qid]["Length"].keys():
            return self.lut[qid]["Length"][member]
        else:
            output = self.visit_ratio[qid] * self.network_throughput(member) * self.waiting_time(qid, member)
            self.lut[qid]["Length"][member] = output
            return output

    def network_throughput(self, member):
        assert(member > 0)
        if member in self.lut["Throughput"].keys():
            return self.lut["Throughput"][member]
        denom = 0.
        for qid, q in enumerate(self.queues):
            denom += self.waiting_time(qid, member) * self.visit_ratio[qid]
        output = member / denom
        self.lut["Throughput"][member] = output
        return output

if __name__ == "__main__":
    mrq = MachineRepairQueue(236745.09, 0.5, 32)
    print(newton(solve_lock_overhead, 0, args=[mrq]))
