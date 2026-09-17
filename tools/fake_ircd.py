#!/usr/bin/env python3
"""A one-client TLS IRC server that plays the part of Azzurra for tools/check_irc.sh."""
import argparse
import socket
import ssl
import sys
import time
import threading

WELCOME = [
    ":fake.azzurra.chat 001 {nick} :Welcome to the Azzurra IRC Network {nick}",
    ":fake.azzurra.chat 005 {nick} CASEMAPPING=ascii NICKLEN=30 :are supported by this server",
    ":fake.azzurra.chat 376 {nick} :End of /MOTD command.",
]


class Server:
    def __init__(self, certificate, key, port, transcript):
        self.started = time.monotonic()
        """Written line by line: the checks watch this file while the bot talks."""
        self.transcript = open(transcript, "w", encoding="utf-8", buffering=1)
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(certificate, key)
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", port))
        self.listener.listen(1)
        self.received = []
        self.nick = "bot"

    def send(self, connection, line):
        connection.sendall((line.format(nick=self.nick) + "\r\n").encode())

    def serve(self, script):
        connection, _ = self.listener.accept()
        with self.context.wrap_socket(connection, server_side=True) as tls:
            tls.settimeout(20)
            pending = b""
            joined = False
            while True:
                try:
                    received = tls.recv(4096)
                except (socket.timeout, ssl.SSLError):
                    break
                if not received:
                    break
                pending += received
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    line = line.decode("utf-8", "replace").rstrip("\r")
                    if not line:
                        continue
                    self.received.append(line)
                    self.transcript.write(line + "\n")
                    print("<< %7.3f %s" % (time.monotonic() - self.started, line), flush=True)
                    parts = line.split(" ")
                    command = parts[0].upper()
                    if command == "NICK":
                        self.nick = parts[1]
                    elif command == "USER":
                        for welcome in WELCOME:
                            self.send(tls, welcome)
                    elif command == "JOIN":
                        self.send(tls, ":{nick}!~u@host JOIN :#test")
                        joined = True
                    elif command == "WHOIS":
                        for reply in script.get("whois", []):
                            self.send(tls, reply.replace("{target}", parts[1]))
                    elif command == "QUIT":
                        return
                if joined and script.get("messages"):
                    for message in script.pop("messages"):
                        self.send(tls, message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--certificate", required=True)
    parser.add_argument("--key", required=True)
    parser.add_argument("--port", type=int, required=True)
    parser.add_argument("--registered", action="store_true")
    parser.add_argument("--message", action="append", default=[])
    parser.add_argument("--transcript", required=True)
    options = parser.parse_args()

    whois = []
    if options.registered:
        whois.append(":fake.azzurra.chat 307 {nick} {target} :has identified for this nick")
    whois.append(":fake.azzurra.chat 318 {nick} {target} :End of /WHOIS list.")

    server = Server(options.certificate, options.key, options.port, options.transcript)
    script = {"whois": whois, "messages": options.message}
    worker = threading.Thread(target=server.serve, args=(script,), daemon=True)
    worker.start()
    worker.join(25)
    server.transcript.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
