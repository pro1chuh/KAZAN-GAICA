from __future__ import annotations

import json
import socket
import sys
from typing import Any, Dict, Protocol


class BotProtocol(Protocol):
    def on_hello(self, data: Dict[str, Any]) -> None: ...
    def on_round_start(self, data: Dict[str, Any]) -> None: ...
    def on_tick(self, data: Dict[str, Any]) -> Dict[str, Any]: ...
    def on_round_end(self, data: Dict[str, Any]) -> None: ...


def run_socket_bot(host: str, port: int, bot: BotProtocol) -> int:
    try:
        print(f"[CLIENT] Connecting to {host}:{port}", file=sys.stderr, flush=True)
        
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((host, port))
        
        print(f"[CLIENT] Connected, sending registration...", file=sys.stderr, flush=True)
        
        reg = {
            "type": "register",
            "name": "SmurfBot",
            "protocol": "standalone"
        }
        sock.send((json.dumps(reg) + "\n").encode())
        print(f"[CLIENT] Registration sent", file=sys.stderr, flush=True)
        
        sock.settimeout(30.0)  # большой таймаут — сервер может долго стартовать раунд
        buffer = ""
        
        while True:
            try:
                # Большой буфер — round_start с картой может быть > 100KB
                chunk = sock.recv(65536).decode('utf-8')
                if not chunk:
                    print(f"[CLIENT] Connection closed by server", file=sys.stderr, flush=True)
                    break
                
                buffer += chunk
                
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    
                    try:
                        msg = json.loads(line)
                    except json.JSONDecodeError as e:
                        print(f"[CLIENT] JSON error: {e} | line[:200]={line[:200]}", file=sys.stderr, flush=True)
                        continue
                    
                    typ = msg.get("type")
                    print(f"[CLIENT] Received: {typ}", file=sys.stderr, flush=True)
                    
                    if typ == "hello":
                        try:
                            bot.on_hello(msg)
                        except Exception as e:
                            print(f"[CLIENT] on_hello error: {e}", file=sys.stderr, flush=True)
                    elif typ == "round_start":
                        try:
                            bot.on_round_start(msg)
                        except Exception as e:
                            print(f"[CLIENT] on_round_start error: {e}", file=sys.stderr, flush=True)
                    elif typ == "tick":
                        try:
                            cmd = bot.on_tick(msg)
                            if cmd:
                                response = json.dumps(cmd) + "\n"
                                sock.send(response.encode('utf-8'))
                                print(f"[CLIENT] Sent command seq={cmd.get('seq')}", file=sys.stderr, flush=True)
                        except Exception as e:
                            import traceback
                            print(f"[CLIENT] on_tick error: {e}", file=sys.stderr, flush=True)
                            traceback.print_exc(file=sys.stderr)
                            # Отправляем пустую команду чтобы не дисконнектиться
                            try:
                                fallback = json.dumps({
                                    "type": "command", "seq": 0,
                                    "move": [0, 0], "aim": [1, 0],
                                    "shoot": False, "kick": False,
                                    "pickup": False, "drop": False, "throw": False
                                }) + "\n"
                                sock.send(fallback.encode('utf-8'))
                            except Exception:
                                pass
                    elif typ == "round_end":
                        try:
                            bot.on_round_end(msg)
                        except Exception as e:
                            print(f"[CLIENT] on_round_end error: {e}", file=sys.stderr, flush=True)
                    else:
                        print(f"[CLIENT] Unknown message type: {typ}", file=sys.stderr, flush=True)
                        
            except socket.timeout:
                print(f"[CLIENT] Socket timeout — waiting for server...", file=sys.stderr, flush=True)
                continue
            except Exception as e:
                print(f"[CLIENT] Recv error: {e}", file=sys.stderr, flush=True)
                import traceback
                traceback.print_exc(file=sys.stderr)
                break
                
    except ConnectionRefusedError:
        print(f"[CLIENT] Connection refused to {host}:{port}", file=sys.stderr, flush=True)
        return 1
    except Exception as e:
        print(f"[CLIENT] Fatal error: {e}", file=sys.stderr, flush=True)
        import traceback
        traceback.print_exc(file=sys.stderr)
        return 1
    
    sock.close()
    return 0