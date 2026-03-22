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
        
        # Используем буферизованное чтение
        sock.settimeout(5.0)
        buffer = ""
        
        while True:
            try:
                data = sock.recv(4096).decode('utf-8')
                if not data:
                    print(f"[CLIENT] Connection closed by server", file=sys.stderr, flush=True)
                    break
                
                buffer += data
                
                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    
                    try:
                        msg = json.loads(line)
                    except json.JSONDecodeError as e:
                        print(f"[CLIENT] JSON error: {e}", file=sys.stderr, flush=True)
                        continue
                    
                    typ = msg.get("type")
                    print(f"[CLIENT] Received: {typ}", file=sys.stderr, flush=True)
                    
                    if typ == "hello":
                        bot.on_hello(msg)
                    elif typ == "round_start":
                        bot.on_round_start(msg)
                    elif typ == "tick":
                        cmd = bot.on_tick(msg)
                        if cmd:
                            response = json.dumps(cmd) + "\n"
                            sock.send(response.encode('utf-8'))
                            print(f"[CLIENT] Sent command seq={cmd.get('seq')}", file=sys.stderr, flush=True)
                    elif typ == "round_end":
                        bot.on_round_end(msg)
                    else:
                        print(f"[CLIENT] Unknown message type: {typ}", file=sys.stderr, flush=True)
                        
            except socket.timeout:
                continue
            except Exception as e:
                print(f"[CLIENT] Error: {e}", file=sys.stderr, flush=True)
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