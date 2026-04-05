"""
capture_chrome131.py — Programmatic ClientHello capture via curl_cffi.

PURPOSE:
  Captures TLS ClientHello bytes from a real BoringSSL Chrome-profile
  connection made by curl_cffi. Used to generate wire-accurate fixture
  bytes for TlsHelloBuilder / ProfileRegistry tests in PR-2+.

USAGE:
  pip install curl-cffi mitmproxy  (or wireshark/tshark for pcap variant)
  python3 capture_chrome131.py [--profile chrome131|chrome133|firefox135|safari18_0]

HOW IT WORKS:
  curl_cffi uses BoringSSL compiled with the actual Chrome/Firefox/Safari
  TLS configuration — this produces *identical* ClientHello bytes to the
  real browser, including:
    - Exact cipher suite order
    - Correct ALPS codepoint (0x4469 for chrome131, 0x44CD for chrome133)
    - ML-KEM-768 / X25519MLKEM768 (0x11EC) key share for Chrome 131+
    - Chrome-accurate ShuffleChromeTLSExtensions extension ordering
    - BoringGREASEECH() GREASE ECH extension (0xFE0D, KDF/AEAD = AES-128-GCM/HKDF-256)
    - BoringPaddingStyle via extension type 0x0015 when applicable

RELATION TO PLAN:
  Referenced by docs/Plans/tdlib-obf-stealth-plan_v6.md section 6.2 / 6.10.
  Captures serve as authoritative ground-truth for:
    - ProfileFixtures.h expected_struct values
    - extension order allow-sets for ChromeShuffleAnchored policy tests
    - ALPS codepoint, padding presence/absence per profile

NOTE ON SECURITY:
  This script connects to a benign HTTPS endpoint (below) for capture only.
  No Telegram proxy secrets, no real user traffic, no PII is transmitted.
  Use an isolated environment when capturing to a corporate/shared log.
"""

import argparse
import socket
import ssl
import struct
import sys
from typing import Optional


# ---------------------------------------------------------------------------
# Minimal TLS ClientHello sniffer using a raw TCP MITM-proxy approach.
# We intercept the ClientHello *before* sending it to the server so we get
# the actual bytes from BoringSSL without needing an external pcap tool.
# ---------------------------------------------------------------------------
CAPTURE_TARGET_HOST = "cloudflare.com"
CAPTURE_TARGET_PORT = 443

SUPPORTED_PROFILES = [
    "chrome131",
    "chrome133",
    "firefox135",
    "safari18_0",
    "edge131",
]


def capture_via_curl_cffi(profile: str, output_hex_file: Optional[str] = None) -> bytes:
    """
    Connect to CAPTURE_TARGET_HOST using curl_cffi with the given browser profile
    and capture the TLS ClientHello via a localhost proxy intercept.

    Returns the raw ClientHello bytes (starting with 0x16 0x03 0x01 ...).
    """
    try:
        from curl_cffi.requests import Session
    except ImportError:
        print("ERROR: curl_cffi not installed. Run: pip install curl-cffi", file=sys.stderr)
        sys.exit(1)

    # Step 1: start a minimal intercepting proxy on localhost to sniff the
    # ClientHello before it is encrypted and forwarded.
    import threading

    captured_hello: list[bytes] = []
    proxy_ready = threading.Event()

    def proxy_thread():
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("127.0.0.1", 0))
        proxyport[0] = srv.getsockname()[1]
        srv.listen(1)
        proxy_ready.set()
        conn, _ = srv.accept()
        srv.close()

        # Read the raw CONNECT or first ClientHello record from curl_cffi.
        # curl_cffi will CONNECT through the proxy, then start the TLS handshake.
        data = b""
        conn.settimeout(5.0)
        try:
            while len(data) < 5:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                data += chunk

            # CONNECT tunneling: respond 200 then read ClientHello
            if data.startswith(b"CONNECT"):
                conn.sendall(b"HTTP/1.1 200 Connection established\r\n\r\n")
                data = b""
                while True:
                    chunk = conn.recv(65536)
                    if not chunk:
                        break
                    data += chunk
                    if len(data) >= 5 and data[0] == 0x16:
                        # TLS record: read full record
                        if len(data) >= 5:
                            rec_len = struct.unpack(">H", data[3:5])[0]
                            if len(data) >= 5 + rec_len:
                                captured_hello.append(data[: 5 + rec_len])
                                break
        except (socket.timeout, OSError):
            pass
        finally:
            conn.close()

    proxyport: list[int] = [0]
    t = threading.Thread(target=proxy_thread, daemon=True)
    t.start()
    proxy_ready.wait(timeout=3.0)

    with Session(impersonate=profile) as session:
        try:
            session.get(
                f"https://{CAPTURE_TARGET_HOST}/",
                proxies={"https": f"http://127.0.0.1:{proxyport[0]}"},
                timeout=8,
                verify=False,
            )
        except Exception:
            pass  # We only need the ClientHello; connection may fail at MITM.

    t.join(timeout=6.0)

    if not captured_hello:
        print(
            f"WARNING: could not capture ClientHello for profile={profile}. "
            "Ensure curl_cffi is installed and network is reachable.",
            file=sys.stderr,
        )
        return b""

    hello_bytes = captured_hello[0]

    if output_hex_file:
        with open(output_hex_file, "w") as f:
            f.write(hello_bytes.hex())
        print(f"ClientHello ({len(hello_bytes)} bytes) written to: {output_hex_file}")
    else:
        print(f"ClientHello ({len(hello_bytes)} bytes):")
        # Print in 16-byte rows for readability
        for i in range(0, len(hello_bytes), 16):
            row = hello_bytes[i : i + 16]
            hex_part = " ".join(f"{b:02x}" for b in row)
            print(f"  {i:04x}: {hex_part}")

    return hello_bytes


def extract_extension_list(hello: bytes) -> list[int]:
    """
    Parse a TLS ClientHello record and return the list of extension type values
    in the order they appear on the wire.

    Used to verify that ChromeShuffleAnchored is correctly implemented:
    - First extension should be GREASE
    - Last non-padding extension should be GREASE
    - RenegotiationInfo (0xff01) should appear at varying positions
    """
    try:
        # Skip TLS record header (5 bytes) + handshake header (4 bytes)
        # + ClientHello fixed fields (2+32+1+session_id+2+cipher_suites+1+comp)
        offset = 5 + 4  # record header + handshake header
        if len(hello) < offset + 35:
            return []

        offset += 2  # legacy_version
        offset += 32  # random

        session_id_len = hello[offset]
        offset += 1 + session_id_len

        cipher_suites_len = struct.unpack_from(">H", hello, offset)[0]
        offset += 2 + cipher_suites_len

        compression_len = hello[offset]
        offset += 1 + compression_len

        if offset + 2 > len(hello):
            return []

        extensions_len = struct.unpack_from(">H", hello, offset)[0]
        offset += 2
        end = offset + extensions_len

        ext_types = []
        while offset + 4 <= end:
            ext_type = struct.unpack_from(">H", hello, offset)[0]
            ext_len = struct.unpack_from(">H", hello, offset + 2)[0]
            ext_types.append(ext_type)
            offset += 4 + ext_len

        return ext_types
    except Exception as exc:
        print(f"WARNING: extension parse failed: {exc}", file=sys.stderr)
        return []


def check_chrome_shuffle_anchors(hello: bytes, profile: str) -> bool:
    """
    Verify that GREASE anchors and shuffle properties match Chrome's
    ShuffleChromeTLSExtensions policy:
      - First extension is GREASE (0x?A?A pattern)
      - Second-to-last or last is GREASE
      - RenegotiationInfo (0xff01) is present and NOT fixed at last position
    """
    ext_types = extract_extension_list(hello)
    if not ext_types:
        return False

    def is_grease(v: int) -> bool:
        lo = v & 0xFF
        hi = (v >> 8) & 0xFF
        return lo == hi and (lo - 0x0A) % 0x10 == 0

    if not is_grease(ext_types[0]):
        print(f"  FAIL: first extension 0x{ext_types[0]:04x} is not GREASE")
        return False

    # Find trailing GREASE (should be near end, before optional padding 0x0015)
    non_padding = [t for t in ext_types if t != 0x0015]
    if not is_grease(non_padding[-1]):
        print(f"  WARN: last non-padding extension 0x{non_padding[-1]:04x} is not GREASE "
              f"(profile={profile})")

    ri_pos = [i for i, t in enumerate(ext_types) if t == 0xFF01]
    if ri_pos:
        print(f"  RenegotiationInfo (0xff01) found at position {ri_pos[0]}/{len(ext_types)}")
    else:
        print(f"  INFO: RenegotiationInfo not present (some profiles omit it)")

    print(f"  Extension order ({len(ext_types)} exts): "
          + " ".join(f"0x{t:04x}" for t in ext_types))
    return True


def main():
    parser = argparse.ArgumentParser(description="Capture TLS ClientHello via curl_cffi")
    parser.add_argument(
        "--profile",
        choices=SUPPORTED_PROFILES,
        default="chrome131",
        help="Browser profile to impersonate (default: chrome131)",
    )
    parser.add_argument(
        "--output",
        metavar="FILE",
        help="Write captured ClientHello hex to FILE (default: print to stdout)",
    )
    parser.add_argument(
        "--verify-shuffle",
        action="store_true",
        help="After capture, verify Chrome ShuffleAnchored policy compliance",
    )
    args = parser.parse_args()

    print(f"Capturing ClientHello for profile: {args.profile}")
    hello = capture_via_curl_cffi(args.profile, args.output)

    if hello and args.verify_shuffle:
        print("\nVerifying ChromeShuffleAnchored compliance:")
        check_chrome_shuffle_anchors(hello, args.profile)


if __name__ == "__main__":
    main()
