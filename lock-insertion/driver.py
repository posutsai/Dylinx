#!/usr/bin/env python3
import sys
import yaml
import itertools
import argparse
import subprocess
import os
import pathlib
import shutil
from argparse import RawTextHelpFormatter

def get_combination(yaml_path):
    with open(yaml_path, "r") as stream:
        meta_data = list(yaml.load_all(stream, Loader=yaml.FullLoader))
    combs = []
    for m in meta_data[0]["LockEntity"]:
        if "lock_combination" in m.keys():
            if "PTHREADMTX" in m["lock_combination"]:
                combs.append([*m["lock_combination"]])
            else:
                combs.append([*m["lock_combination"], "PTHREADMTX"])
        else:
            combs.append(["PTHREADMTX"])
    # combs = [ tuple(m["lock_combination"]) if "lock_combination" in m.keys() else ("PTHREADMTX", ) for m in meta_data[0]["LockEntity"]]
    for comb in itertools.product(*combs):
        current_iter = []
        for i, c in enumerate(comb):
            current_iter.append("#define DYLINX_LOCK_MACRO_{} dylinx_{}lock_t".format(i, c.lower()))
        yield '\n'.join(current_iter)


def brute_force(cc_path, out_dir, build_inst, clean_inst):
    header_start = "#ifndef __DYLINX_ITERATE_LOCK_COMB__\n#define __DYLINX_ITERATE_LOCK_COMB__"
    header_end = "#endif // __DYLINX_ITERATE_LOCK_COMB__"
    # Generate dylinx-insertion.yaml
    exe_path = os.environ["DYLINX_GLUE_PATH"] + "/build/bin/dylinx"
    with subprocess.Popen(args=[exe_path, cc_path, f"{out_dir}/dylinx-insertion.yaml"], stdout=subprocess.PIPE) as proc:
        out = proc.stdout.read().decode("utf-8").split('\n')[0]
        print(out)
    os.chdir(str(pathlib.PurePath(cc_path).parent))
    for comb in get_combination(f"{out_dir}/dylinx-insertion.yaml"):
        print(comb)
        with open("{}/src/glue/dylinx-runtime-config.h".format(os.environ["DYLINX_GLUE_PATH"]), "w") as rt_config:
            rt_config.write(f"{header_start}\n\n{comb}\n\n{header_end}")
        os.environ["C_INCLUDE_PATH"] = os.environ["DYLINX_GLUE_PATH"]
        with subprocess.Popen(args=build_inst.split(' '), stdout=subprocess.PIPE) as proc:
            out = proc.stdout.read().decode("utf-8")
        print(out)
        if clean_inst != None:
            with subprocess.Popen(args=clean_inst.split(' '), stdout=subprocess.PIPE) as proc:
                out = proc.stdout.read().decode("utf-8")
            print(out)

def optimize_locks(args):
    with open(args.config_path, "r") as yaml_file:
        conf = list(yaml.load_all(yaml_file, Loader=yaml.FullLoader))[0]
        cc_path = conf["compile_commands_path"]
        out_dir = conf["output_directory_path"]
        build_inst = conf["instructions"][0]["build"]
        clean_inst = conf["instructions"][1]["clean"]
    brute_force(cc_path, out_dir, build_inst, clean_inst)

def revert(args):
    with open("{}/dylinx-insertion.yaml".format(args.output_dir), "r") as f:
        for f in list(yaml.load_all(f, Loader=yaml.FullLoader))[0]["AlteredFiles"]:
            p = pathlib.PurePath(f)
            shutil.copyfile(str(p.parent) + "/.dylinx/" + p.name, str(p))
            os.remove(str(p.parent) + "/.dylinx/" + p.name)
            if len(os.listdir(str(p.parent) + "/.dylinx")) == 0:
                shutil.rmtree(str(p.parent) + "/.dylinx")

def fix_comb(args):
    if args.fixed_comb == -1:
        print(
            "In fix mode, user should specify which combination is going"
            "to be fixed."
        )
        sys.exit()
    with open(args.config_path, "r") as yaml_file:
        conf = list(yaml.load_all(yaml_file, Loader=yaml.FullLoader))[0]
        cc_path = conf["compile_commands_path"]
        out_dir = conf["output_directory_path"]
        build_inst = conf["instructions"][0]["build"]
        clean_inst = conf["instructions"][1]["clean"]

    header_start = "#ifndef __DYLINX_ITERATE_LOCK_COMB__\n#define __DYLINX_ITERATE_LOCK_COMB__"
    header_end = "#endif // __DYLINX_ITERATE_LOCK_COMB__"
    # Generate dylinx-insertion.yaml
    os.chdir(str(pathlib.PurePath(cc_path).parent))
    comb = list(get_combination(f"{out_dir}/dylinx-insertion.yaml"))[args.fixed_comb]
    print(f"======== #{args.fixed_comb: 3d} combination ======")
    print(comb)
    with open("{}/src/glue/dylinx-runtime-config.h".format(os.environ["DYLINX_GLUE_PATH"]), "w") as rt_config:
        rt_config.write(f"{header_start}\n\n{comb}\n\n{header_end}")
    os.environ["C_INCLUDE_PATH"] = os.environ["DYLINX_GLUE_PATH"] + "/src/glue"
    os.environ["LD_LIBRARY_PATH"] = os.environ["DYLINX_GLUE_PATH"] + "/build/lib"
    with subprocess.Popen(args=build_inst.split(' '), stdout=subprocess.PIPE) as proc:
        out = proc.stdout.read().decode("utf-8")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(formatter_class=RawTextHelpFormatter)
    parser.add_argument(
        "-m",
        "--mode",
        type=str,
        choices=["reverse", "optimize_locks", "fix"],
        required=True,
        help="Configuring the operating mode."
    )
    parser.add_argument(
        "-c",
        "--config_path",
        type=str,
        default=os.getcwd() + "/dylinx-config.yaml",
        help=
        "Specify the path of subject's config file. It should contain following component"
        "\n1. compile_commands_path"
        "\n2. output_directory_path"
        "\n3. instructions"
        "\n   - build_commands"
        "\n   - clean_commands"
    )
    parser.add_argument(
        "-f",
        "--fixed_comb",
        type=int,
        default=-1,

    )
    args = parser.parse_args()
    if args.mode == "optimize_locks":
        optimize_locks(args)
    if args.mode == "reverse":
        revert(args)
    if args.mode == "fix":
        fix_comb(args)
