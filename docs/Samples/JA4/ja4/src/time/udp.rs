// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

//! Timestamps obtained from a QUIC stream

use crate::{
    time::{Fingerprints, PacketTimestamp, Ttl},
    Packet, Result, Sender,
};

#[derive(Debug)]
pub(crate) enum Timestamps {
    Collecting(Expect),
    Done(Fingerprints),
}

impl Default for Timestamps {
    fn default() -> Self {
        Self::Collecting(Expect::ClientInitial(state::ClientInitial))
    }
}

#[derive(Debug)]
pub(crate) enum Expect {
    /// Initial state. Waiting for an Initial QUIC packet sent by the client.
    ClientInitial(state::ClientInitial),
    /// Waiting for an Initial QUIC packet sent by the server.
    ServerInitial(state::ServerInitial),
    /// Waiting for a Handshake packet from the server.
    ServerHandshake(state::ServerHandshake),
    /// Waiting for a Handshake packet from either the server or the client.
    Handshake(state::Handshake),
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
                    Expect::ClientInitial(st) => st.apply(t),
                    Expect::ServerInitial(st) => st.apply(t),
                    Expect::ServerHandshake(st) => st.apply(t),
                    Expect::Handshake(st) => st.apply(t),
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

    /// Initial state. Waiting for an Initial QUIC packet sent by the client.
    #[derive(Debug)]
    pub(crate) struct ClientInitial;

    impl From<ClientInitial> for Timestamps {
        fn from(st: ClientInitial) -> Self {
            Expect::ClientInitial(st).into()
        }
    }

    impl ClientInitial {
        pub(super) fn apply(self, t: Timestamp) -> Timestamps {
            if let Timestamp::ClientInitial((t_a, client_ttl)) = t {
                ServerInitial { t_a, client_ttl }.into()
            } else {
                self.into()
            }
        }
    }

    /// Waiting for an Initial QUIC packet sent by the server.
    #[derive(Debug)]
    pub(crate) struct ServerInitial {
        t_a: PacketTimestamp,
        client_ttl: Ttl,
    }

    impl From<ServerInitial> for Timestamps {
        fn from(st: ServerInitial) -> Self {
            Expect::ServerInitial(st).into()
        }
    }

    impl ServerInitial {
        pub(super) fn apply(self, t: Timestamp) -> Timestamps {
            if let Timestamp::ServerInitial((t_b, server_ttl)) = t {
                let Self { t_a, client_ttl } = self;
                ServerHandshake {
                    t_a,
                    t_b,
                    client_ttl,
                    server_ttl,
                }
                .into()
            } else {
                Expect::ServerInitial(self).into()
            }
        }
    }

    /// Waiting for a Handshake packet from the server.
    #[derive(Debug)]
    pub(crate) struct ServerHandshake {
        t_a: PacketTimestamp,
        t_b: PacketTimestamp,
        client_ttl: Ttl,
        server_ttl: Ttl,
    }

    impl From<ServerHandshake> for Timestamps {
        fn from(st: ServerHandshake) -> Self {
            Expect::ServerHandshake(st).into()
        }
    }

    impl ServerHandshake {
        pub(super) fn apply(self, t: Timestamp) -> Timestamps {
            if let Timestamp::ServerHandshake(t_c) = t {
                let Self {
                    t_a,
                    t_b,
                    client_ttl,
                    server_ttl,
                } = self;
                Handshake {
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

    /// Waiting for a Handshake packet from either the server or the client
    #[derive(Debug)]
    pub(crate) struct Handshake {
        t_a: PacketTimestamp,
        t_b: PacketTimestamp,
        t_c: PacketTimestamp,
        client_ttl: Ttl,
        server_ttl: Ttl,
    }

    impl From<Handshake> for Timestamps {
        fn from(st: Handshake) -> Self {
            Expect::Handshake(st).into()
        }
    }

    impl Handshake {
        pub(super) fn apply(mut self, t: Timestamp) -> Timestamps {
            match t {
                Timestamp::ServerHandshake(t_c) => {
                    self.t_c = t_c;
                    self.into()
                }
                Timestamp::ClientHandshake(t_d) => {
                    let Self {
                        t_a,
                        t_b,
                        t_c,
                        client_ttl,
                        server_ttl,
                    } = self;
                    Done {
                        t_a,
                        t_b,
                        t_c,
                        t_d,
                        client_ttl,
                        server_ttl,
                    }
                    .into()
                }
                Timestamp::ClientInitial(_) | Timestamp::ServerInitial(_) => self.into(),
            }
        }
    }

    /// Final state. All 4 timestamps have been collected.
    #[derive(Debug)]
    struct Done {
        t_a: PacketTimestamp,
        t_b: PacketTimestamp,
        t_c: PacketTimestamp,
        t_d: PacketTimestamp,
        client_ttl: Ttl,
        server_ttl: Ttl,
    }

    impl From<Done> for Timestamps {
        fn from(st: Done) -> Self {
            let Done {
                t_a,
                t_b,
                t_c,
                t_d,
                client_ttl,
                server_ttl,
            } = st;

            let ja4l_c = (t_d.timestamp - t_c.timestamp) / 2;
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
    /// First, the client sends an Initial QUIC packet.
    ClientInitial((PacketTimestamp, Ttl)),
    /// Then the server responds with its Initial packet.
    ServerInitial((PacketTimestamp, Ttl)),
    /// The server sends several Handshake packets to the client.
    ServerHandshake(PacketTimestamp),
    /// The client's second packet, a Handshake packet.
    ClientHandshake(PacketTimestamp),
}

impl Timestamp {
    fn from_packet(pkt: &Packet) -> Result<Option<Self>> {
        let Some(quic) = pkt.find_proto("quic") else {
            return Ok(None);
        };

        // XXX-FIXME(vvv): Some packets (e.g. GRE) may have several "udp" layers.
        // We should take the last one, not the first one.

        // SAFETY: We would not reach this point if the packet didn't have a "udp" layer;
        // see `Streams::update` and `StreamAttrs::new`. It is safe to unwrap.
        let udp = pkt.find_proto("udp").unwrap();

        let sender = if udp.first("udp.dstport")? == "443" {
            Sender::Client
        } else if udp.first("udp.srcport")? == "443" {
            Sender::Server
        } else {
            // Neither of the ports is 443, so it's not a QUIC packet.
            return Ok(None);
        };

        let Ok(packet_type) = quic.first("quic.long.packet_type") else {
            // Some QUIC packet do not have this field. Ignore them.
            return Ok(None);
        };
        const INITIAL: &str = "0";
        const HANDSHAKE: &str = "2";

        let t = || PacketTimestamp::new(pkt);

        Ok(match (sender, packet_type) {
            // First, the client sends an Initial QUIC packet. This timestamp is "A".
            (Sender::Client, INITIAL) => Some(Self::ClientInitial((t()?, Ttl::new(pkt)?))),
            // Then the server responds with its Initial packet. This timestamp is "B".
            (Sender::Server, INITIAL) => Some(Self::ServerInitial((t()?, Ttl::new(pkt)?))),
            // The server sends several Handshake packets to the client. This could be 1-5
            // packets, depending on the server. The last packet from the server before
            // the client sends a packet is "C".
            //
            // Look to see if the client has sent a second packet. If so, then the timestamp
            // of the last packet that the server sent is "C".
            (Sender::Server, HANDSHAKE) => Some(Self::ServerHandshake(t()?)),
            // The client's second packet, a Handshake packet, is "D".
            (Sender::Client, HANDSHAKE) => Some(Self::ClientHandshake(t()?)),
            _ => None,
        })
    }
}
