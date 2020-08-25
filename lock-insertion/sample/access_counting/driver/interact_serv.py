#!/usr/local/bin/python3
import sys
import argparse
from argparse import RawTextHelpFormatter
import os
from flask import Flask
from Dylinx import blueprint_gen

def main(args):
    app = Flask(__name__)
    app.register_blueprint(blueprint_gen())
    app.run(host="0.0.0.0", port=5566)

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
    args = parser.parse_args()
    main(args)
