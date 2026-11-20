from __future__ import annotations

import base64
import hashlib
import hmac
import re
import socket
import socketserver
import threading
import time
import uuid
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Callable

import psutil

# Absolute imports from the spec's other files are NOT included here per instructions.
# This file is self-contained regarding its own logic but relies on standard library.

NS_SOAP = "http://www.w3.org/2003/05/soap-envelope"
NS_WSA = "http://schemas.xmlsoap.org/ws/2004/08/addressing"
NS_DISCOVERY = "http://schemas.xmlsoap.org/ws/2005/04/discovery"
NS_ONVIF_NET = "http://www.onvif.org/ver10/network/wsdl"
NS_TDS = "http://www.onvif.org/ver10/device/wsdl"
NS_TRT = "http://www.onvif.org/ver10/media/wsdl"
NS_TT = "http://www.onvif.org/ver10/schema"
NS_ERR = "http://www.onvif.org/ver10/error"


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]


def _envelope_ns(soap_ns: str) -> str:
    return (f'xmlns:s="{soap_ns}" xmlns:tds="{NS_TDS}" xmlns:trt="{NS_TRT}" '
            f'xmlns:tt="{NS_TT}" xmlns:ter="{NS_ERR}"')


@dataclass
class OnvifConfig:
    name: str
    serial: str
    manufacturer: str
    model: str
    username: str
    password: str
    onvif_port: int
    rtsp_port: int
    profiles: list[dict] = field(default_factory=list)

class _SoapServer(ThreadingHTTPServer):
    allow_reuse_address = True
    daemon_threads = True

    def server_bind(self) -> None:
        # The stock server_bind looks up the host name, which stalls for seconds
        # in a namespace with no name service.
        socketserver.TCPServer.server_bind(self)
        self.server_name, self.server_port = self.server_address[:2]


class OnvifResponder:
    def __init__(self, config: OnvifConfig, log: Callable[[str], None]):
        self.config = config
        self.log = log
        self.requests_seen: list[str] = []
        
        # Discovery state
        self._discovery_sock: socket.socket | None = None
        self._discovery_thread: threading.Thread | None = None
        self._stop_discovery = threading.Event()
        
        # SOAP state
        self._soap_server: _SoapServer | None = None
        self._soap_thread: threading.Thread | None = None
        self._stop_soap = threading.Event()

    def start(self) -> None:
        self._start_discovery()
        self._start_soap()

    def stop(self) -> None:
        self._stop_discovery.set()
        if self._soap_server:
            self._soap_server.shutdown()
            self._soap_server.server_close()
        self._soap_server = None
        if self._discovery_sock:
            self._discovery_sock.close()
            self._discovery_sock = None
        if self._discovery_thread and self._discovery_thread.is_alive():
            self._discovery_thread.join(timeout=3)

    def _start_discovery(self) -> None:
        self._discovery_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self._discovery_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._discovery_sock.bind(("", 3702))
        # A short timeout lets the loop notice stop() and rejoin the group after an address change.
        self._discovery_sock.settimeout(1.0)
        
        # Join multicast group on all non-loopback interfaces
        addrs = self._get_local_ips()
        for addr in addrs:
            mreq = socket.inet_aton("239.255.255.250") + socket.inet_aton(addr)
            try:
                self._discovery_sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
            except OSError:
                pass

        self._stop_discovery.clear()
        self._discovery_thread = threading.Thread(target=self._discovery_loop, daemon=True)
        self._discovery_thread.start()

    def _start_soap(self) -> None:
        handler = type("BoundHandler", (OnvifSOAPHandler,), {"responder": self})
        self._soap_server = _SoapServer(("0.0.0.0", self.config.onvif_port), handler)
        
        self._stop_soap.clear()
        self._soap_thread = threading.Thread(target=self._soap_loop, daemon=True)
        self._soap_thread.start()

    def _join_group(self, sock: socket.socket) -> None:
        for ip in self._get_local_ips():
            mreq = socket.inet_aton("239.255.255.250") + socket.inet_aton(ip)
            try:
                sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
            except OSError:
                pass  # already joined, or the interface is down

    def _discovery_loop(self) -> None:
        last_join = 0.0
        while not self._stop_discovery.is_set():
            sock = self._discovery_sock
            if sock is None:
                break
            if time.monotonic() - last_join > 2:
                # Rejoin regularly: the interface can get a new address at any time.
                self._join_group(sock)
                last_join = time.monotonic()
            try:
                data, sender = sock.recvfrom(8192)
            except socket.timeout:
                continue
            except OSError:
                break
            text = data.decode("utf-8", errors="ignore")
            if "Probe" not in text or "ProbeMatch" in text:
                continue
            match = re.search(r"<(?:[\w-]+:)?MessageID[^>]*>([^<]+)<", text)
            if not match:
                continue
            try:
                # The client drops replies whose XAddrs host is not the packet's source
                # address, so use the local address that routes to the sender.
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
                    route.connect(sender)
                    my_ip = route.getsockname()[0]
                xaddr = f"http://{my_ip}:{self.config.onvif_port}/onvif/device_service"
                reply = self._build_probe_reply(match.group(1).strip(), sender[0]).replace("{XADDR}", xaddr)
                sock.sendto(reply.encode("utf-8"), sender)
            except OSError as exc:
                self.log(f"discovery reply to {sender[0]} failed: {exc}")
                continue
            self.requests_seen.append("Probe")
            self.log(f"onvif Probe from {sender[0]}")

    def _soap_loop(self) -> None:
        if self._soap_server:
            self._soap_server.serve_forever()

    def _get_local_ips(self) -> list[str]:
        # Every non-loopback IPv4 address; the camera's interface may get its address after start.
        ips = []
        for addrs in psutil.net_if_addrs().values():
            for addr in addrs:
                if addr.family == socket.AF_INET and not addr.address.startswith("127."):
                    ips.append(addr.address)
        return ips

    def _build_probe_reply(self, relates_to: str, sender_ip: str) -> str:
        endpoint_uuid = uuid.uuid5(uuid.NAMESPACE_DNS, self.config.serial).hex
        msg_uuid = uuid.uuid4().hex
        
        scopes = f"onvif://www.onvif.org/type/video_encoder onvif://www.onvif.org/Profile/Streaming onvif://www.onvif.org/name/{self.config.name} onvif://www.onvif.org/hardware/{self.config.model}"
        
        xml = f"""<s:Envelope xmlns:s="http://www.w3.org/2003/05/soap-envelope" xmlns:a="http://schemas.xmlsoap.org/ws/2004/08/addressing">
  <s:Header>
    <a:MessageID>urn:uuid:{msg_uuid}</a:MessageID>
    <a:RelatesTo>{relates_to}</a:RelatesTo>
    <a:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</a:To>
    <a:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</a:Action>
  </s:Header>
  <s:Body>
    <d:ProbeMatches xmlns:d="http://schemas.xmlsoap.org/ws/2005/04/discovery">
      <d:ProbeMatch>
        <a:EndpointReference>
          <a:Address>urn:uuid:{endpoint_uuid}</a:Address>
        </a:EndpointReference>
        <d:Types>dn:NetworkVideoTransmitter</d:Types>
        <d:Scopes>{scopes}</d:Scopes>
        <d:XAddrs>{{XADDR}}</d:XAddrs>
        <d:MetadataVersion>1</d:MetadataVersion>
      </d:ProbeMatch>
    </d:ProbeMatches>
  </s:Body>
</s:Envelope>"""
        return xml

class OnvifSOAPHandler(BaseHTTPRequestHandler):
    responder: OnvifResponder
    
    def log_message(self, format, *args):
        pass # Silence default logging

    def do_POST(self):
        length = int(self.headers.get("Content-Length", 0) or 0)
        raw = self.rfile.read(length)
        try:
            root = ET.fromstring(raw)
        except ET.ParseError:
            self.send_error(400, "Bad Request")
            return

        # Answer in the SOAP version the client used.
        soap_ns = root.tag[1:].split("}")[0] if root.tag.startswith("{") else NS_SOAP
        body = next((e for e in root if _local(e.tag) == "Body"), None)
        request = next(iter(body), None) if body is not None else None
        if request is None:
            self.send_error(400, "No SOAP Body")
            return
        name = _local(request.tag)
        responder = self.responder
        responder.requests_seen.append(name)
        responder.log(f"onvif {name} from {self.client_address[0]}")

        config = responder.config
        if config.username and name != "GetSystemDateAndTime":
            if not self._check_auth(root, config.username, config.password):
                self._reply(401, soap_ns, self._fault(soap_ns, "NotAuthorized", "NotAuthorized: FailedAuthentication"))
                return

        host = self.connection.getsockname()[0]
        port = config.onvif_port
        token_elem = next((e for e in request.iter() if _local(e.tag) == "ProfileToken"), None)
        token = (token_elem.text or "").strip() if token_elem is not None else ""
        known = {p["token"] for p in config.profiles}

        if name == "GetSystemDateAndTime":
            resp = self._get_system_date_time()
        elif name == "GetDeviceInformation":
            resp = self._get_device_information()
        elif name == "GetCapabilities":
            resp = self._get_capabilities(host, port)
        elif name == "GetServices":
            resp = self._get_services(host, port)
        elif name == "GetProfiles":
            resp = self._get_profiles()
        elif name == "GetProfile" and token in known:
            resp = self._get_profile(token)
        elif name == "GetStreamUri" and token in known:
            resp = self._get_stream_uri(token, host, port)
        elif name == "GetNetworkInterfaces":
            resp = self._get_network_interfaces()
        elif name in ("GetProfile", "GetStreamUri"):
            self._reply(400, soap_ns, self._fault(soap_ns, "NoProfile", f"NoProfile: {token}"))
            return
        else:
            self._reply(500, soap_ns, self._fault(soap_ns, "ActionNotSupported", f"ActionNotSupported: {name}"))
            return

        self._reply(200, soap_ns, f"<s:Envelope {_envelope_ns(soap_ns)}><s:Body>{resp}</s:Body></s:Envelope>")

    def _reply(self, status: int, soap_ns: str, text: str) -> None:
        data = text.encode("utf-8")
        content_type = "application/soap+xml" if soap_ns == NS_SOAP else "text/xml"
        self.send_response(status)
        self.send_header("Content-Type", f"{content_type}; charset=utf-8")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(data)

    def _check_auth(self, root: ET.Element, username: str, password: str) -> bool:
        found: dict[str, ET.Element] = {}
        for elem in root.iter():
            found.setdefault(_local(elem.tag), elem)
        user, pw = found.get("Username"), found.get("Password")
        if user is None or pw is None or (user.text or "").strip() != username:
            return False
        given = (pw.text or "").strip()
        if pw.get("Type", "").endswith("PasswordDigest"):
            nonce, created = found.get("Nonce"), found.get("Created")
            if nonce is None or created is None:
                return False
            try:
                nonce_bytes = base64.b64decode((nonce.text or "").strip())
            except ValueError:
                return False
            digest = hashlib.sha1(nonce_bytes + (created.text or "").strip().encode() + password.encode()).digest()
            return hmac.compare_digest(given, base64.b64encode(digest).decode())
        return hmac.compare_digest(given, password)

    def _fault(self, soap_ns: str, code: str, reason: str) -> str:
        if soap_ns == NS_SOAP:
            detail = (f"<s:Code><s:Value>s:Receiver</s:Value><s:Subcode><s:Value>ter:{code}</s:Value></s:Subcode></s:Code>"
                      f"<s:Reason><s:Text xml:lang=\"en\">{reason}</s:Text></s:Reason>")
        else:
            detail = f"<faultcode>s:Server</faultcode><faultstring>{reason}</faultstring>"
        return f"<s:Envelope {_envelope_ns(soap_ns)}><s:Body><s:Fault>{detail}</s:Fault></s:Body></s:Envelope>"

    def _get_system_date_time(self) -> str:
        now = datetime.now(timezone.utc)
        tz = "UTC0"
        return f"""<tds:GetSystemDateAndTimeResponse xmlns:tds="{NS_TDS}" xmlns:tt="{NS_TT}">
  <tds:SystemDateAndTime>
    <tt:DateTimeType>NTP</tt:DateTimeType>
    <tt:DaylightSavings>false</tt:DaylightSavings>
    <tt:TimeZone>
      <tt:TZ>{tz}</tt:TZ>
    </tt:TimeZone>
    <tt:UTCDateTime>
      <tt:Time>
        <tt:Hour>{now.hour}</tt:Hour>
        <tt:Minute>{now.minute}</tt:Minute>
        <tt:Second>{now.second}</tt:Second>
      </tt:Time>
      <tt:Date>
        <tt:Year>{now.year}</tt:Year>
        <tt:Month>{now.month}</tt:Month>
        <tt:Day>{now.day}</tt:Day>
      </tt:Date>
    </tt:UTCDateTime>
  </tds:SystemDateAndTime>
</tds:GetSystemDateAndTimeResponse>"""

    def _get_device_information(self) -> str:
        c = self.responder.config
        return f"""<tds:GetDeviceInformationResponse xmlns:tds="{NS_TDS}" xmlns:tt="{NS_TT}">
  <tds:Manufacturer>{c.manufacturer}</tds:Manufacturer>
  <tds:Model>{c.model}</tds:Model>
  <tds:FirmwareVersion>1.0.0</tds:FirmwareVersion>
  <tds:SerialNumber>{c.serial}</tds:SerialNumber>
  <tds:HardwareId>{c.model}</tds:HardwareId>
</tds:GetDeviceInformationResponse>"""

    def _get_capabilities(self, host: str, port: int) -> str:
        return f"""<tds:GetCapabilitiesResponse xmlns:tds="{NS_TDS}" xmlns:tt="{NS_TT}">
  <tds:Capabilities>
    <tt:Device>
      <tt:XAddr>http://{host}:{port}/onvif/device_service</tt:XAddr>
    </tt:Device>
    <tt:Media>
      <tt:XAddr>http://{host}:{port}/onvif/media_service</tt:XAddr>
      <tt:StreamingCapabilities>
        <tt:RTPMulticast>false</tt:RTPMulticast>
        <tt:RTP_TCP>true</tt:RTP_TCP>
        <tt:RTP_RTSP_TCP>true</tt:RTP_RTSP_TCP>
      </tt:StreamingCapabilities>
    </tt:Media>
  </tds:Capabilities>
</tds:GetCapabilitiesResponse>"""

    def _get_services(self, host: str, port: int) -> str:
        return f"""<tds:GetServicesResponse xmlns:tds="{NS_TDS}" xmlns:tt="{NS_TT}">
  <tds:Service>
    <tds:Namespace>{NS_TDS}</tds:Namespace>
    <tds:XAddr>http://{host}:{port}/onvif/device_service</tds:XAddr>
    <tds:Version>
      <tt:Major>2</tt:Major>
      <tt:Minor>0</tt:Minor>
    </tds:Version>
  </tds:Service>
  <tds:Service>
    <tds:Namespace>{NS_TRT}</tds:Namespace>
    <tds:XAddr>http://{host}:{port}/onvif/media_service</tds:XAddr>
    <tds:Version>
      <tt:Major>2</tt:Major>
      <tt:Minor>0</tt:Minor>
    </tds:Version>
  </tds:Service>
</tds:GetServicesResponse>"""

    def _get_profiles(self) -> str:
        profiles_xml = ""
        for p in self.responder.config.profiles:
            token = p["token"]
            name = p["token"]
            width = p["width"]
            height = p["height"]
            fps = p["fps"]
            bitrate = p["bitrate_kbps"]
            encoding = p["encoding"]
            
            h264_elem = ""
            if encoding == "H264":
                h264_elem = f"""<tt:H264>
  <tt:GovLength>{fps}</tt:GovLength>
  <tt:H264Profile>Baseline</tt:H264Profile>
</tt:H264>"""
            
            profiles_xml += f"""<trt:Profiles token="{token}" fixed="true" xmlns:trt="{NS_TRT}">
  <tt:Name>{name}</tt:Name>
  <tt:VideoSourceConfiguration token="vsc">
    <tt:Name>VideoSourceConfig</tt:Name>
    <tt:UseCount>2</tt:UseCount>
    <tt:SourceToken>vs0</tt:SourceToken>
    <tt:Bounds x="0" y="0" width="{width}" height="{height}"/>
  </tt:VideoSourceConfiguration>
  <tt:VideoEncoderConfiguration token="vec_{token}">
    <tt:Name>VideoEncoderConfig</tt:Name>
    <tt:UseCount>1</tt:UseCount>
    <tt:Encoding>{encoding}</tt:Encoding>
    <tt:Resolution>
      <tt:Width>{width}</tt:Width>
      <tt:Height>{height}</tt:Height>
    </tt:Resolution>
    <tt:Quality>5</tt:Quality>
    <tt:RateControl>
      <tt:FrameRateLimit>{fps}</tt:FrameRateLimit>
      <tt:EncodingInterval>1</tt:EncodingInterval>
      <tt:BitrateLimit>{bitrate}</tt:BitrateLimit>
    </tt:RateControl>
    {h264_elem}
    <tt:SessionTimeout>PT60S</tt:SessionTimeout>
  </tt:VideoEncoderConfiguration>
</trt:Profiles>"""
        
        return f"""<trt:GetProfilesResponse xmlns:trt="{NS_TRT}">
  {profiles_xml}
</trt:GetProfilesResponse>"""

    def _get_profile(self, token: str) -> str:
        for p in self.responder.config.profiles:
            if p["token"] == token:
                # Reuse GetProfiles logic but wrap in GetProfileResponse
                width = p["width"]
                height = p["height"]
                fps = p["fps"]
                bitrate = p["bitrate_kbps"]
                encoding = p["encoding"]
                
                h264_elem = ""
                if encoding == "H264":
                    h264_elem = f"""<tt:H264>
  <tt:GovLength>{fps}</tt:GovLength>
  <tt:H264Profile>Baseline</tt:H264Profile>
</tt:H264>"""
                
                profile_xml = f"""<trt:Profile token="{token}" fixed="true" xmlns:trt="{NS_TRT}">
  <tt:Name>{p['token']}</tt:Name>
  <tt:VideoSourceConfiguration token="vsc">
    <tt:Name>VideoSourceConfig</tt:Name>
    <tt:UseCount>2</tt:UseCount>
    <tt:SourceToken>vs0</tt:SourceToken>
    <tt:Bounds x="0" y="0" width="{width}" height="{height}"/>
  </tt:VideoSourceConfiguration>
  <tt:VideoEncoderConfiguration token="vec_{token}">
    <tt:Name>VideoEncoderConfig</tt:Name>
    <tt:UseCount>1</tt:UseCount>
    <tt:Encoding>{encoding}</tt:Encoding>
    <tt:Resolution>
      <tt:Width>{width}</tt:Width>
      <tt:Height>{height}</tt:Height>
    </tt:Resolution>
    <tt:Quality>5</tt:Quality>
    <tt:RateControl>
      <tt:FrameRateLimit>{fps}</tt:FrameRateLimit>
      <tt:EncodingInterval>1</tt:EncodingInterval>
      <tt:BitrateLimit>{bitrate}</tt:BitrateLimit>
    </tt:RateControl>
    {h264_elem}
    <tt:SessionTimeout>PT60S</tt:SessionTimeout>
  </tt:VideoEncoderConfiguration>
</trt:Profile>"""
                return f"""<trt:GetProfileResponse xmlns:trt="{NS_TRT}">
  {profile_xml}
</trt:GetProfileResponse>"""
        
        fault = self._make_fault("InvalidArgVal", "Unknown ProfileToken")
        return fault

    def _get_stream_uri(self, token: str, host: str, port: int) -> str:
        for p in self.responder.config.profiles:
            if p["token"] == token:
                path = p["path"]
                rtsp_port = self.responder.config.rtsp_port
                uri = f"rtsp://{host}:{rtsp_port}{path}"
                return f"""<trt:GetStreamUriResponse xmlns:trt="{NS_TRT}" xmlns:tt="{NS_TT}">
  <trt:MediaUri>
    <tt:Uri>{uri}</tt:Uri>
    <tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>
    <tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>
    <tt:Timeout>PT60S</tt:Timeout>
  </trt:MediaUri>
</trt:GetStreamUriResponse>"""
        fault = self._make_fault("InvalidArgVal", "Unknown ProfileToken")
        return fault

    def _get_network_interfaces(self) -> str:
        hw_addr = ""
        try:
            with open("/sys/class/net/eth0/address", "r") as f:
                hw_addr = f.read().strip()
        except FileNotFoundError:
            pass
        
        return f"""<tds:GetNetworkInterfacesResponse xmlns:tds="{NS_TDS}" xmlns:tt="{NS_TT}">
  <tds:NetworkInterfaces token="eth0">
    <tt:Enabled>true</tt:Enabled>
    <tt:Info>
      <tt:Name>eth0</tt:Name>
      <tt:HwAddress>{hw_addr}</tt:HwAddress>
    </tt:Info>
  </tds:NetworkInterfaces>
</tds:GetNetworkInterfacesResponse>"""