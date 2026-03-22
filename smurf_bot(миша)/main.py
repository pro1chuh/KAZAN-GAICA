#!/usr/bin/env python3
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))

from smurf_bot import TerminatorBot
from smurf_bot.client import run_socket_bot


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("host", nargs="?", default=None)
    parser.add_argument("port", nargs="?", type=int, default=None)
    args = parser.parse_args()

    host = args.host or os.getenv("GAICA_BOT_HOST", "127.0.0.1")
    port = args.port or int(os.getenv("GAICA_BOT_PORT", "0"))

    if port <= 0:
        print("No port", file=sys.stderr)
        return 1

    print(f"[MAIN] Connecting to {host}:{port}", file=sys.stderr)
    bot = TerminatorBot()
    return run_socket_bot(host, port, bot)


if __name__ == "__main__":
    sys.exit(main())