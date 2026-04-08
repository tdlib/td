// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

//! Light distance (latency) fingerprints -- JA4L-C (client), JA4L-S (server)

mod tcp;
mod udp;

use serde::Serialize;

use crate::{Packet, PacketNum, Result};
pub(crate) use {tcp::Timestamps as TcpTimestamps, udp::Timestamps as UdpTimestamps};

#[derive(Debug, Serialize)]
pub(crate) struct Fingerprints {
    ja4l_c: String,
    ja4l_s: String,
}

pub(crate) trait Timestamps: Default {
    fn update(self, pkt: &Packet) -> Result<Self>
    where
        Self: Sized;

    /// Returns the JA4L-C and JA4L-S fingerprints.
    fn finish(self) -> Option<Fingerprints>;
}

#[derive(Debug)]
pub(crate) struct PacketTimestamp {
    #[allow(dead_code)]
    packet: PacketNum,
    pub(crate) timestamp: i64,
}

impl PacketTimestamp {
    pub(crate) fn new(pkt: &Packet) -> Result<Self> {
        Ok(Self {
            packet: pkt.num,
            timestamp: pkt.timestamp_micros()?,
        })
    }
}

#[derive(Debug)]
// "ip.ttl" and "ipv6.hlim" are `u8`.
//
// Proofs:
//
// - https://github.com/wireshark/wireshark/blob/385e37ce0ef24058c8f0cceab0ba7b76b1b70624/epan/dissectors/packet-ip.c#L2639
// - https://github.com/wireshark/wireshark/blob/385e37ce0ef24058c8f0cceab0ba7b76b1b70624/epan/dissectors/packet-ipv6.c#L3830-L3831
pub(crate) struct Ttl(u8);

impl Ttl {
    // XXX-FIXME(vvv): Some packets (e.g. GRE) may have several "ip" layers.
    // We should take the last one, not the first one.
    pub(crate) fn new(pkt: &Packet) -> Result<Self> {
        let ttl = if let Some(ip) = pkt.find_proto("ip") {
            ip.first("ip.ttl")?.parse::<u8>()?
        } else if let Some(ipv6) = pkt.find_proto("ipv6") {
            // "hlim" stands for "Hop Limit"
            ipv6.first("ipv6.hlim")?.parse::<u8>()?
        } else {
            // SAFETY: We've established in `StreamAttrs::new` that either "ip" or "ipv6"
            // layer exists.
            panic!("BUG");
        };
        Ok(Self(ttl))
    }
}
