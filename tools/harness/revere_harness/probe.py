from __future__ import annotations

import json
import re
import socket
import uuid
import subprocess
import time


def ffprobe_streams(url: str, timeout: float = 15.0) -> list[dict]:
    try:
        proc = subprocess.run(
            ["ffprobe", "-v", "error", "-rtsp_transport", "tcp", "-show_streams", "-of", "json", url],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        if proc.returncode != 0:
            return []
        data = json.loads(proc.stdout)
        return data.get("streams", [])
    except (subprocess.TimeoutExpired, json.JSONDecodeError, FileNotFoundError):
        return []


def onvif_probe(timeout: float = 3.0, target: str | None = None, expect: int | None = None) -> list[dict]:
    probe_msg = (
        '<?xml version="1.0" encoding="utf-8"?>'
        '<soap:Envelope xmlns:soap="http://www.w3.org/2003/05/soap-envelope"'
        ' xmlns:wsa="http://schemas.xmlsoap.org/ws/2004/08/addressing"'
        ' xmlns:wsd="http://schemas.xmlsoap.org/ws/2005/04/discovery">'
        '<soap:Header>'
        '<wsa:MessageID>urn:uuid:' + str(uuid.uuid4()) + '</wsa:MessageID>'
        '<wsa:To>urn:schemas-xmlsoap-org:ws:2004:08:addressing:role/anonymous</wsa:To>'
        '</soap:Header>'
        '<soap:Body><wsd:Probe xmlns:dn="http://www.onvif.org/ver10/network/wsdl">'
        '<wsd:Types>dn:NetworkVideoTransmitter</wsd:Types></wsd:Probe></soap:Body>'
        '</soap:Envelope>'
    )

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        if target is None:
            sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            sock.bind(("10.77.0.1", 0))
            sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_IF, socket.inet_aton("10.77.0.1"))
            dest = ("239.255.255.250", 3702)
        else:
            sock.bind(("10.77.0.1", 0))
            dest = (target, 3702)

        started = time.monotonic()
        sock.sendto(probe_msg.encode("utf-8"), dest)
        sock.settimeout(timeout)

        replies: list[dict] = []
        seen_senders: set[str] = set()
        
        while True:
            remaining = timeout - (time.monotonic() - started)
            if remaining <= 0:
                break
            
            try:
                sock.settimeout(remaining)
                data, addr = sock.recvfrom(4096)
                
                if expect is not None and len(seen_senders) >= expect:
                    break
                    
                sender_ip = addr[0]
                if sender_ip in seen_senders:
                    continue
                seen_senders.add(sender_ip)

                text = data.decode("utf-8", errors="replace")
                xaddrs_match = re.search(r'<(?:\w+:)?XAddrs>([^<]+)<', text)
                scopes_match = re.search(r'<(?:\w+:)?Scopes>([^<]+)<', text)
                
                if xaddrs_match:
                    elapsed = time.monotonic() - started
                    replies.append({
                        "from": sender_ip,
                        "xaddrs": xaddrs_match.group(1),
                        "scopes": scopes_match.group(1) if scopes_match else "",
                        "seconds": elapsed,
                    })
                    
            except socket.timeout:
                break
    finally:
        sock.close()

    return replies


def picture_change(url: str, seconds: int = 3, timeout: float = 40.0) -> float | None:
    """Mean brightness difference between frames one second apart; 0.0 for a still picture."""
    cmd = [
        "ffmpeg", "-v", "error", "-rtsp_transport", "tcp", "-i", url,
        "-t", str(seconds),
        "-vf", "fps=1,crop=iw:ih-60:0:60,tblend=all_mode=difference,signalstats,metadata=print:key=lavfi.signalstats.YAVG:file=-",
        "-f", "null", "-"
    ]
    try:
        proc = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            timeout=timeout,
        )
        if proc.returncode != 0:
            return None
        
        values = []
        for line in proc.stdout.splitlines():
            match = re.search(r'YAVG=(\S+)', line)
            if match:
                try:
                    values.append(float(match.group(1)))
                except ValueError:
                    pass
        
        if not values:
            return None
            
        return sum(values) / len(values)
    except (subprocess.TimeoutExpired, FileNotFoundError):
        return None