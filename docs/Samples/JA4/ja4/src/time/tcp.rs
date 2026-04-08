// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

//! Timestamps obtained from a TCP handshake

use crate::{
    time::{Fingerprints, PacketTimestamp, Ttl},
    Packet, Result,
};

#[derive(Debug)]
pub(crate) enum Timestamps {
    Collecting(Expect),
    Done(Fingerprints),
}

impl Default for Timestamps {
    fn default() -> Self {
        Self::Collecting(Expect::Syn(state::Syn))
    }
}

#[derive(Debug)]
pub(crate) enum Expect {
    /// Initial state. Waiting for a SYN.
    Syn(state::Syn),
    /// Waiting for a SYN-ACK.
    SynAck(state::SynAck),
    /// Waiting for an ACK.
    Ack(state::Ack),
}

impl From<Expect> for Timestamps {
    fn from(st: Expect) -> Self {
        Self::Collecting(st)
    }
}

impl crate::time::Timestamps for Timestamps {
    fn update(self, pkt: &Packet) -> Result<Self> {
        match self {
            done @ Self::Done(_) => Ok(done),
            Self::Collecting(expect) => {
                let Some(t) = Timestamp::from_packet(pkt)? else {
                    return Ok(Self::Collecting(expect));
                };
                Ok(match expect {
                    Expect::Syn(st) => st.apply(t),
                    Expect::SynAck(st) => st.apply(t),
                    Expect::Ack(st) => st.apply(t),
                })
            }
        }
    }

    fn finish(self) -> Option<Fingerprints> {
        match self {
            Self::Collecting(_) => None,
            Self::Done(fps) => Some(fps),
        }
    }
}

mod state {
    use super::{Expect, Fingerprints, PacketTimestamp, Timestamp, Timestamps, Ttl};

    /// Initial state. Waiting for a SYN.
    #[derive(Debug)]
    pub(crate) struct Syn;

    impl From<Syn> for Timestamps {
        fn from(st: Syn) -> Self {
            Expect::Syn(st).into()
        }
    }

    impl Syn {
        pub(super) fn apply(self, t: Timestamp) -> Timestamps {
            if let Timestamp::Syn((t_a, client_ttl)) = t {
                SynAck { t_a, client_ttl }.into()
            } else {
                self.into()
            }
        }
    }

    /// Waiting for a SYN-ACK.
    #[derive(Debug)]
    pub(crate) struct SynAck {
        t_a: PacketTimestamp,
        client_ttl: Ttl,
    }

    impl From<SynAck> for Timestamps {
        fn from(st: SynAck) -> Self {
            Expect::SynAck(st).into()
        }
    }

    impl SynAck {
        pub(super) fn apply(self, t: Timestamp) -> Timestamps {
            if let Timestamp::SynAck((t_b, server_ttl)) = t {
                let Self { t_a, client_ttl } = self;
                Ack {
                    t_a,
                    t_b,
                    client_ttl,
                    server_ttl,
                }
                .into()
            } else {
                Expect::SynAck(self).into()
            }
        }
    }

    /// Waiting for an ACK.
    #[derive(Debug)]
    pub(crate) struct Ack {
        t_a: PacketTimestamp,
        t_b: PacketTimestamp,
        client_ttl: Ttl,
        server_ttl: Ttl,
    }

    impl From<Ack> for Timestamps {
        fn from(st: Ack) -> Self {
            Expect::Ack(st).into()
        }
    }

    impl Ack {
        pub(super) fn apply(self, t: Timestamp) -> Timestamps {
            if let Timestamp::Ack(t_c) = t {
                let Self {
                    t_a,
                    t_b,
                    client_ttl,
                    server_ttl,
                } = self;
                Done {
                    t_a,
                    t_b,
                    t_c,
                    client_ttl,
                    server_ttl,
                }
                .into()
            } else {
                self.into()
            }
        }
    }

    /// Final state. All 3 timestamps have been collected.
    #[derive(Debug)]
    struct Done {
        t_a: PacketTimestamp,
        t_b: PacketTimestamp,
        t_c: PacketTimestamp,
        client_ttl: Ttl,
        server_ttl: Ttl,
    }

    impl From<Done> for Timestamps {
        fn from(st: Done) -> Self {
            let Done {
                t_a,
                t_b,
                t_c,
                client_ttl,
                server_ttl,
            } = st;

            let ja4l_c = (t_c.timestamp - t_b.timestamp) / 2;
            debug_assert!(ja4l_c >= 0); // 0 if the difference == 1

            let ja4l_s = (t_b.timestamp - t_a.timestamp) / 2;
            debug_assert!(ja4l_s >= 0); // 0 if the difference == 1

            Self::Done(Fingerprints {
                ja4l_c: format!("{ja4l_c}_{client_ttl}", client_ttl = client_ttl.0),
                ja4l_s: format!("{ja4l_s}_{server_ttl}", server_ttl = server_ttl.0),
            })
        }
    }
}

#[derive(Debug)]
enum Timestamp {
    /// First the client sends a SYN packet.
    Syn((PacketTimestamp, Ttl)),
    /// Then the server responds with a SYN-ACK packet.
    SynAck((PacketTimestamp, Ttl)),
    /// Then the client responds with an ACK packet, completing the TCP 3-way handshake.
    Ack(PacketTimestamp),
}

impl Timestamp {
    fn from_packet(pkt: &Packet) -> Result<Option<Self>> {
        let Some(tcp) = pkt.find_proto("tcp") else {
            return Ok(None);
        };

        let t = || PacketTimestamp::new(pkt);

        let ack = tcp.first("tcp.flags.ack")?;
        let syn = tcp.first("tcp.flags.syn")?;
        Ok(match (syn, ack) {
            ("1", "0") | ("True", "False") => Some(Self::Syn((t()?, Ttl::new(pkt)?))),
            ("1", "1") | ("True", "True") => Some(Self::SynAck((t()?, Ttl::new(pkt)?))),
            ("0", "1") | ("False", "True") => Some(Self::Ack(t()?)),
            _ => None,
        })
    }
}
