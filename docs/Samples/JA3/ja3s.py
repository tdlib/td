#!/usr/bin/env python
"""Generate JA3 fingerprints from PCAPs using Python."""

import argparse
import dpkt
import json
import socket
import struct
import os
from hashlib import md5
from distutils.version import LooseVersion

__author__ = "Tommy Stallings"
__copyright__ = "Copyright (c) 2017, salesforce.com, inc."
__credits__ = ["John B. Althouse", "Jeff Atkinson", "Josh Atkins"]
__license__ = "BSD 3-Clause License"
__version__ = "1.0.1"
__maintainer__ = "Tommy Stallings, Brandon Dixon"
__email__ = "tommy.stallings2@gmail.com"


SSL_PORT = 443
TLS_HANDSHAKE = 22


def convert_ip(value):
    """Convert an IP address from binary to text.

    :param value: Raw binary data to convert
    :type value: str
    :returns: str
    """
    try:
        return socket.inet_ntop(socket.AF_INET, value)
    except ValueError:
        return socket.inet_ntop(socket.AF_INET6, value)


def process_extensions(server_handshake):
    """Process any extra extensions and convert to a JA3 segment.

    :param client_handshake: Handshake data from the packet
    :type client_handshake: dpkt.ssl.TLSClientHello
    :returns: list
    """
    if not hasattr(server_handshake, "extensions"):
        # Needed to preserve commas on the join
        return [""]

    exts = list()
    for ext_val, ext_data in server_handshake.extensions:
        exts.append(ext_val)

    results = list()
    results.append("-".join([str(x) for x in exts]))
    return results


def process_pcap(pcap, any_port=False):
    """Process packets within the PCAP.

    :param pcap: Opened PCAP file to be processed
    :type pcap: dpkt.pcap.Reader
    :param any_port: Whether or not to search for non-SSL ports
    :type any_port: bool
    """
    decoder = dpkt.ethernet.Ethernet
    linktype = pcap.datalink()
    if linktype == dpkt.pcap.DLT_LINUX_SLL:
        decoder = dpkt.sll.SLL
    elif linktype == dpkt.pcap.DLT_NULL or linktype == dpkt.pcap.DLT_LOOP:
        decoder = dpkt.loopback.Loopback

    results = list()
    for timestamp, buf in pcap:
        try:
            eth = decoder(buf)
        except Exception:
            continue

        if not isinstance(eth.data, (dpkt.ip.IP, dpkt.ip6.IP6)):
            # We want an IP packet
            continue
        if not isinstance(eth.data.data, dpkt.tcp.TCP):
            # TCP only
            continue

        ip = eth.data
        tcp = ip.data

        if not (tcp.dport == SSL_PORT or tcp.sport == SSL_PORT or any_port):
            # Doesn't match SSL port or we are picky
            continue
        if len(tcp.data) <= 0:
            continue

        tls_handshake = bytearray(tcp.data)
        if tls_handshake[0] != TLS_HANDSHAKE:
            continue

        records = list()

        try:
            records, bytes_used = dpkt.ssl.tls_multi_factory(tcp.data)
        except dpkt.ssl.SSL3Exception:
            continue
        except dpkt.dpkt.NeedData:
            continue

        if len(records) <= 0:
            continue

        for record in records:
            if record.type != TLS_HANDSHAKE:
                continue
            if len(record.data) == 0:
                continue
            server_hello = bytearray(record.data)
            if server_hello[0] != 2:
                # We only want server HELLO
                continue
            try:
                handshake = dpkt.ssl.TLSHandshake(record.data)
            except dpkt.dpkt.NeedData:
                # Looking for a handshake here
                continue
            if not isinstance(handshake.data, dpkt.ssl.TLSServerHello):
                # Still not the HELLO
                continue

            server_handshake = handshake.data
            ja3 = [str(server_handshake.version)]

            # Cipher Suites (16 bit values)
            if LooseVersion(dpkt.__version__) <= LooseVersion('1.9.1'):
                ja3.append(str(server_handshake.cipher_suite))
            else:
                ja3.append(str(server_handshake.ciphersuite.code))
            ja3 += process_extensions(server_handshake)
            ja3 = ",".join(ja3)

            record = {"source_ip": convert_ip(ip.src),
                      "destination_ip": convert_ip(ip.dst),
                      "source_port": tcp.sport,
                      "destination_port": tcp.dport,
                      "ja3": ja3,
                      "ja3_digest": md5(ja3.encode()).hexdigest(),
                      "timestamp": timestamp}
            results.append(record)

    return results


def main():
    """Intake arguments from the user and print out JA3 output."""
    desc = "A python script for extracting JA3 fingerprints from PCAP files"
    parser = argparse.ArgumentParser(description=(desc))
    parser.add_argument("pcap", help="The pcap file to process")
    help_text = "Look for client hellos on any port instead of just 443"
    parser.add_argument("-a", "--any_port", required=False,
                        action="store_true", default=False,
                        help=help_text)
    help_text = "Print out as JSON records for downstream parsing"
    parser.add_argument("-j", "--json", required=False, action="store_true",
                        default=False, help=help_text)
    args = parser.parse_args()

    # Use an iterator to process each line of the file
    output = None
    with open(args.pcap, 'rb') as fp:
        try:
            capture = dpkt.pcap.Reader(fp)
        except ValueError as e_pcap:
            try:
                fp.seek(0, os.SEEK_SET)
                capture = dpkt.pcapng.Reader(fp)
            except ValueError as e_pcapng:
                raise Exception(
                        "File doesn't appear to be a PCAP or PCAPng: %s, %s" %
                        (e_pcap, e_pcapng))
        output = process_pcap(capture, any_port=args.any_port)

    if args.json:
        output = json.dumps(output, indent=4, sort_keys=True)
        print(output)
    else:
        for record in output:
            tmp = '[{dest}:{port}] JA3S: {segment} --> {digest}'
            tmp = tmp.format(dest=record['destination_ip'],
                             port=record['destination_port'],
                             segment=record['ja3'],
                             digest=record['ja3_digest'])
            print(tmp)


if __name__ == "__main__":
        main()
