import argparse
import sys

from smurf_bot.client import BotClient
from smurf_bot.controller import BotController


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("host")
    parser.add_argument("port", type=int)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    controller = BotController()
    client = BotClient(args.host, args.port, controller)
    client.run()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
