#!/usr/local/bin/python3
import sys
import argparse
from argparse import RawTextHelpFormatter
import os

if f"{os.environ['DYLINX_HOME']}/python" not in sys.path:
    sys.path.append(f"{os.environ['DYLINX_HOME']}/python")
from Dylinx import NaiveSubject

def main(args):
    subject = NaiveSubject(args.config_path)
    for i in range(subject.get_num_perm()):
        subject.step(i)


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
        default=f"{os.getcwd()}/dylinx-config.yaml",
        help=
        "Specify the path of subject's config file. It should contain following component"
        "\n1. compile_commands_path"
        "\n2. output_directory_path"
        "\n3. instructions"
        "\n   - build_commands"
        "\n   - clean_commands"
    )
    args = parser.parse_args()
    main(args)
