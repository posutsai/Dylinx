#!/usr/local/bin/python3
import numpy as np
from scipy.special import beta
from scipy.special import factorial

def solve_lock_overhead(delta, q_model, duration):
    eq_cs = q_model.critical_time + delta
    serv_rate = 1. / eq_cs
    rho = q_model.exp_factor / serv_rate
    p0 = beta(q_model.n_core, 1. / rho)
    us = 1 - p0
    return (1. / serv_rate) * ((q_model.n_core / us) - (1 + rho) / rho) - duration

class MachineRepairQueue:
    def __init__(self, critical_time, ratio, n_core):
        self.critical_time = critical_time
        self.parallel_time = critical_time / ratio
        self.exp_factor = 1. / self.parallel_time
        self.serv_rate = 1. / critical_time
        self.rho = self.exp_factor / self.serv_rate
        self.n_core = n_core

    # def waiting_time(self):
    #     utility = self.utility
    #     n_core = self.n_core
    #     arr_rate = self.arr_rate
    #     assert(self.utility != 1)
    #     p0 = (1 - utility) / (1 - utility ** (n_core + 1))
    #     L = (utility / (1 - utility)) * ((n_core + 1) * utility ** (n_core + 1) / (1 - utility ** n_core + 1))
    #     return L / (arr_rate * (1 - utility ** n_core * p0))

    # def approximate_waiting_time(self):
    #     p0 = beta(self.n_core, 1. / self.rho)
    #     us = 1 - p0
    #     return (1. / self.serv_rate) * ((self.n_core / us) - (1 + self.rho) / self.rho)
    #
    # def compute_waiting_time(self):
    #     denom = 0.
    #     for i in range(self.n_core + 1):
    #         denom += (factorial(self.n_core) / factorial(self.n_core - i)) * (self.exp_factor / self.serv_rate) ** i
    #     p0 = 1. / denom
    #     us = 1 - p0
    #     return (1. / self.serv_rate) * ((self.n_core / us) - (1 + self.rho) / self.rho)


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
