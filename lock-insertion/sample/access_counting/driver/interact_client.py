#!/usr/local/bin/python3
import sys
import requests
import os

def main():
    res = requests.post(
        f'{os.environ["DYLINX_DOMAIN"]}:5566/init'
        , json={
            "config_path": f"{os.environ['DYLINX_HOME']}/sample/access_counting/dylinx-config.yaml",
            "mod": "Dylinx",
            "cls": "NaiveSubject"
        }
    )
    if res.ok:
        print(res.json())
    res = requests.get(f'{os.environ["DYLINX_DOMAIN"]}:5566/perm')
    if res.ok:
        print(res.json())

