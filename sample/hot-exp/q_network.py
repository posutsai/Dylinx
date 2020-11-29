#!/usr/local/bin/python3
import numpy as np

class Queue:
    def __init__(self, duration):
        self.duration = duration
        self.serv_rate = 1. / self.duration

class InfiniteQueue(Queue):
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

q_p = InfiniteQueue(3)
q_1 = Queue(1)
q_2 = Queue(2)
P = [
    [0., 0.4, 0.6],
    [1., 0., 0.],
    [1., 0., 0.],
]
network = QNetwork([q_p, q_1, q_2], P)
print(network.waiting_time(1, 1))
print(network.waiting_time(2, 1))
print(network.waiting_time(1, 80))
print(network.waiting_time(2, 80))
