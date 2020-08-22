#!/usr/local/bin/python3
import sys
import requests
import os
res = requests.post(
    f'{os.environ["DYLINX_DOMAIN"]}:5566/init'
    , json={"config_path": f"{os.environ['DYLINX_HOME']}/dylinx-config.yaml"}
)

res = requests.get(f'{os.environ["DYLINX_DOMAIN"]}:5566/perm')
if res.ok:
    print(res.json())

