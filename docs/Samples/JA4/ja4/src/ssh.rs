// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

//! JA4SSH -- SSH traffic fingerprinting

use std::collections::HashMap;

use serde::Serialize;

use crate::{Packet, Result, Sender};

#[derive(Debug, Default)]
pub(crate) struct Stream {
    /// Statistics obtained from up to [`crate::conf::ConfSsh::sample_size`] packets.
    stats: Stats,
    /// SSH fingerprints.
    ///
    /// New entry is added every [`crate::conf::ConfSsh::sample_size`] packets.
    ja4ssh: Vec<Fingerprint>,
    extras: StreamExtras,
}

impl Stream {
    pub(crate) fn update(
        &mut self,
        pkt: &Packet,
        sender: Sender,
        sample_size: usize,
    ) -> Result<()> {
        self.stats.update(pkt, sender)?;
        if self.stats.nr_ssh_client_packets + self.stats.nr_ssh_server_packets == sample_size {
            let stats = std::mem::take(&mut self.stats);
            if let Some(fp) = stats.into() {
                self.ja4ssh.push(fp);
            }
        }
        self.extras.update(pkt, sender);
        Ok(())
    }

    pub(crate) fn finish(self) -> (Vec<Fingerprint>, Option<Extras>) {
        let Stream {
            stats: counts,
            mut ja4ssh,
            extras,
        } = self;
        if let Some(fp) = counts.into() {
            ja4ssh.push(fp);
        }
        (ja4ssh, extras.try_into().ok())
    }
}

#[derive(Debug, Default, Serialize)]
pub(crate) struct Extras {
    /// HASSH fingerprint (SSH client).
    hassh: Option<String>,
    /// HASSH fingerprint (SSH server).
    hassh_server: Option<String>,
    ssh_protocol_client: Option<String>,
    ssh_protocol_server: Option<String>,
    encryption_algorithm: Option<String>,
}

impl TryFrom<StreamExtras> for Extras {
    type Error = ();

    fn try_from(extras: StreamExtras) -> Result<Self, Self::Error> {
        let StreamExtras {
            hassh,
            hassh_server,
            ssh_protocol_client,
            ssh_protocol_server,
            encryption,
        } = extras;
        let encryption_algorithm = match encryption {
            None => None,
            Some(Encryption::ClientToServerAlgorithms(_)) => None,
            Some(Encryption::SelectedAlgorithm(alg)) => Some(alg),
        };
        if hassh.is_none()
            && hassh_server.is_none()
            && ssh_protocol_client.is_none()
            && ssh_protocol_server.is_none()
            && encryption_algorithm.is_none()
        {
            Err(())
        } else {
            Ok(Extras {
                hassh,
                hassh_server,
                ssh_protocol_client,
                ssh_protocol_server,
                encryption_algorithm,
            })
        }
    }
}

#[derive(Debug)]
enum Encryption {
    /// The `ssh.encryption_algorithms_client_to_server` list sent from the client message.
    ClientToServerAlgorithms(Vec<String>),
    /// The first `ssh.encryption_algorithms_server_to_client` sent in the server message that is
    /// on the `ssh.encryption_algorithms_client_to_server` list sent from the client message.
    SelectedAlgorithm(String),
}

/// Additional information from a SSH stream that hasn't been [finished] yet.
///
/// [finished]: Stream::finish
#[derive(Debug, Default)]
struct StreamExtras {
    hassh: Option<String>,
    hassh_server: Option<String>,
    ssh_protocol_client: Option<String>,
    ssh_protocol_server: Option<String>,
    encryption: Option<Encryption>,
}

impl StreamExtras {
    fn update(&mut self, pkt: &Packet, sender: Sender) {
        let Some(ssh) = pkt.find_proto("ssh") else {
            return;
        };

        #[cfg(debug_assertions)]
        if let Ok(dir) = ssh.find("ssh.direction") {
            match sender {
                Sender::Client => assert_eq!(dir.display(), "Direction: client-to-server"),
                Sender::Server => assert_eq!(dir.display(), "Direction: server-to-client"),
            }
        }

        match sender {
            Sender::Client => {
                if let Ok(s) = ssh.first("ssh.kex.hassh") {
                    debug_assert!(self.hassh.is_none());
                    self.hassh = Some(s.to_owned());
                }
                if let Ok(s) = ssh.first("ssh.encryption_algorithms_client_to_server") {
                    // An SSH stream can have at most one client message with this field,
                    // and the client message precedes any server messages.
                    debug_assert!(self.encryption.is_none());
                    self.encryption = Some(Encryption::ClientToServerAlgorithms(
                        s.split(',').map(|s| s.to_owned()).collect(),
                    ));
                }
                if let Ok(s) = ssh.first("ssh.protocol") {
                    debug_assert!(self.ssh_protocol_client.is_none());
                    self.ssh_protocol_client = Some(s.to_owned());
                }
            }
            Sender::Server => {
                if let Ok(s) = ssh.first("ssh.kex.hasshserver") {
                    debug_assert!(self.hassh_server.is_none());
                    self.hassh_server = Some(s.to_owned());
                }
                if let Ok(s) = ssh.first("ssh.protocol") {
                    debug_assert!(self.ssh_protocol_server.is_none());
                    self.ssh_protocol_server = Some(s.to_owned());
                }
                let Ok(server_algs) = ssh.first("ssh.encryption_algorithms_server_to_client")
                else {
                    return;
                };
                match &self.encryption {
                    None => {
                        // We haven't seen the SSH client message with `ssh.encryption_algorithms_client_to_server` field.
                        // Apparently the capture file doesn't have the beginning of this
                        // SSH stream.
                    }
                    Some(Encryption::SelectedAlgorithm(_)) => {
                        #[cfg(debug_assertions)]
                        panic!("BUG: packet={}: an SSH stream can have at most one server message with `ssh.encryption_algorithms_server_to_client` field", pkt.num);
                    }
                    Some(Encryption::ClientToServerAlgorithms(client_algs)) => {
                        if let Some(selected_alg) = server_algs.split(',').find(|server_alg| {
                            client_algs
                                .iter()
                                .any(|client_alg| client_alg == server_alg)
                        }) {
                            self.encryption =
                                Some(Encryption::SelectedAlgorithm(selected_alg.to_owned()));
                        }
                    }
                }
            }
        }
    }
}

#[derive(Debug, Default)]
struct Stats {
    /// Key -- client TCP payload length, bytes; value -- number of packets with this length.
    /// Notes:
    ///
    /// - tshark exposes TCP payload length as `tcp.len`.
    /// - We only take SSH packets (layer 7) into account.
    client_tcp_len_counts: HashMap<usize, usize>,
    /// Key -- server TCP payload length; value -- number of packets with this length.
    server_tcp_len_counts: HashMap<usize, usize>,

    /// Total number of SSH packets sent from the client.
    nr_ssh_client_packets: usize,
    /// Total number of SSH packets sent from the server.
    nr_ssh_server_packets: usize,

    /// Total number of ACK packets sent from the client.
    nr_tcp_client_acks: usize,
    /// Total number of ACK packets sent from the server.
    nr_tcp_server_acks: usize,
}

impl Stats {
    fn update(&mut self, pkt: &Packet, sender: Sender) -> Result<()> {
        const BARE_ACK_FLAG: &str = "0x0010";

        // SAFETY: We would not reach this point if the packet didn't have a "tcp" layer;
        // see `Streams::update` and `StreamId2::new`. It's safe to unwrap.
        let tcp = pkt.find_proto("tcp").unwrap();

        if pkt.find_proto("ssh").is_some() {
            let tcp_len = tcp.first("tcp.len")?.parse()?;
            match sender {
                Sender::Client => {
                    *self.client_tcp_len_counts.entry(tcp_len).or_default() += 1;
                    self.nr_ssh_client_packets += 1;
                }
                Sender::Server => {
                    *self.server_tcp_len_counts.entry(tcp_len).or_default() += 1;
                    self.nr_ssh_server_packets += 1;
                }
            }
        } else if tcp.first("tcp.flags")? == BARE_ACK_FLAG {
            match sender {
                Sender::Client => self.nr_tcp_client_acks += 1,
                Sender::Server => self.nr_tcp_server_acks += 1,
            }
        }
        Ok(())
    }
}

/// JA4SSH fingerprint.
#[derive(Debug, Serialize)]
pub(crate) struct Fingerprint(String);

impl From<Stats> for Option<Fingerprint> {
    fn from(counters: Stats) -> Self {
        let Stats {
            client_tcp_len_counts,
            server_tcp_len_counts,
            nr_ssh_client_packets,
            nr_ssh_server_packets,
            nr_tcp_client_acks,
            nr_tcp_server_acks,
        } = counters;

        if client_tcp_len_counts.is_empty() && server_tcp_len_counts.is_empty() {
            // This doesn't seem to be an *SSH* TCP stream after all.
            return None;
        }

        // We’re looking for the mode, or the value that appears the most number of times
        // in the data set.
        //
        // E.g., if 36 bytes appear 20 times, and 128 bytes appear 10 times, and 200 bytes
        // appear 15 times, the mode is 36. If there is a collision, we choose the smaller
        // byte value.
        //
        // Reference: https://github.com/FoxIO-LLC/ja4/blob/16850cc2c8bcb8328c1a43a851a3a9a6eaa56103/technical_details/JA4SSH.md#how-to-measure-the-mode-for-tcp-payload-lengths-across-200-packets-in-the-session
        let mode_client = min_key_with_max_value(client_tcp_len_counts).unwrap_or(0);
        let mode_server = min_key_with_max_value(server_tcp_len_counts).unwrap_or(0);

        let fp = format!(
            "c{mode_client}s{mode_server}_c{nr_ssh_client_packets}s{nr_ssh_server_packets}_c{nr_tcp_client_acks}s{nr_tcp_server_acks}"
        );
        Some(Fingerprint(fp))
    }
}

fn min_key_with_max_value(kvs: impl IntoIterator<Item = (usize, usize)>) -> Option<usize> {
    let mut max_v = 0;
    let mut min_k_with_max_v = None;

    for (k, v) in kvs {
        #[allow(clippy::comparison_chain)]
        if v > max_v {
            min_k_with_max_v = Some(k);
            max_v = v;
        } else if v == max_v {
            match min_k_with_max_v {
                None => min_k_with_max_v = Some(k),
                Some(k0) => {
                    if k < k0 {
                        min_k_with_max_v = Some(k);
                    }
                }
            }
        }
    }
    min_k_with_max_v
}

#[test]
fn test_min_key_with_max_value() {
    assert_eq!(min_key_with_max_value([]), None);

    let kvs = [(36, 20), (128, 10), (200, 15)];
    assert_eq!(min_key_with_max_value(kvs), Some(36));

    // Collisions, case 1:
    let mut kvs = [
        (21, 1),
        (16, 1),
        (64, 1),
        (144, 1),
        (24, 1),
        (48, 1),
        (792, 1),
    ];
    assert_eq!(min_key_with_max_value(kvs), Some(16));
    kvs.sort();
    assert_eq!(min_key_with_max_value(kvs), Some(16));

    // Collisions, case 2:
    let mut kvs = [(23, 1), (152, 1), (48, 1), (640, 1), (464, 1)];
    assert_eq!(min_key_with_max_value(kvs), Some(23));
    kvs.sort();
    assert_eq!(min_key_with_max_value(kvs), Some(23));
}
