#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
import sys

# Добавляем путь к папке smurf_bot
sys.path.insert(0, os.path.dirname(__file__))

from smurf_bot import AdvancedBot
from smurf_bot.client import run_socket_bot


def main():
    parser = argparse.ArgumentParser(description="Smurf Bot for GAICA")
    parser.add_argument("host", nargs="?", default=None)
    parser.add_argument("port", nargs="?", type=int, default=None)
    args = parser.parse_args()

    # Получаем хост и порт из аргументов
    if args.host is None and args.port is None:
        # Пробуем из переменных окружения
        host = os.getenv("GAICA_BOT_HOST", os.getenv("BOT_HOST", "127.0.0.1"))
        port = int(os.getenv("GAICA_BOT_PORT", os.getenv("BOT_PORT", "0")) or "0")
    else:
        host = args.host
        port = args.port

    print(f"[MAIN] Starting Smurf Bot, connecting to {host}:{port}", file=sys.stderr, flush=True)

    if port <= 0:
        print("[MAIN] No port specified", file=sys.stderr, flush=True)
        return 1

    bot = AdvancedBot()
    return run_socket_bot(host, port, bot)


if __name__ == "__main__":
    sys.exit(main())