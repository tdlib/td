// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

//! JA4 (TLS client), JA4S (TLS server), and JA4X (X.509 certificate) fingerprinting

use std::fmt;

use itertools::Itertools as _;
use ja4x::x509_parser::{certificate::X509Certificate, prelude::FromDer as _};
use serde::Serialize;
use tracing::{debug, warn};

use crate::{Error, FormatFlags, Packet, PacketNum, Proto, Result};

#[derive(Debug, Default)]
pub(crate) struct Stream {
    pub(crate) client: Option<ClientStats>,
    pub(crate) server: Option<ServerStats>,
    pub(crate) x509: Vec<X509Stats>,
}

impl Stream {
    pub(crate) fn update(&mut self, pkt: &Packet, store_pkt_num: bool) -> Result<()> {
        // Some QUIC frames contain fragmented TLS protocols that do not have `tls.handshake.type` field:
        //
        // ```xml
        //    <field name="quic.frame" showname="CRYPTO" size="39" pos="887" show="" value="">
        //      <field name="quic.frame_type" showname="Frame Type: CRYPTO (0x0000000000000006)" size="1" pos="887" show="6" value="06"/>
        //      <field name="quic.crypto.offset" showname="Offset: 0" size="1" pos="888" show="0" value="00"/>
        //      <field name="quic.crypto.length" showname="Length: 36" size="1" pos="889" show="36" value="24"/>
        //      <field name="quic.crypto.crypto_data" showname="Crypto Data" size="36" pos="890" show="" value=""/>
        //      <proto name="tls" showname="TLSv1.3 Record Layer: Handshake Protocol: Client Hello (fragment)" size="36" pos="890">
        //        <field name="tls.handshake" showname="Handshake Protocol: Client Hello (fragment)" size="36" pos="890" show="" value=""/>
        //      </proto>
        //    </field>
        // ```
        //
        // Because of that, we should not use `Packet::find_proto` --- it returns the first proto,
        // which may not have `tls.handshake.type` field.
        let Some(tls) = pkt
            .protos("tls")
            .find(|tls| tls.find("tls.handshake.type").is_ok())
        else {
            return Ok(());
        };

        const CLIENT_HELLO: &str = "1";
        const SERVER_HELLO: &str = "2";
        const CERTIFICATE: &str = "11";

        for tls_handshake_type in tls.fields("tls.handshake.type") {
            match tls_handshake_type.value() {
                CLIENT_HELLO => {
                    debug_assert_eq!(
                        tls_handshake_type.display(),
                        "Handshake Type: Client Hello (1)",
                        "packet={}",
                        pkt.num
                    );
                    // We only process a single TLS Client Hello packet per stream.
                    if self.client.is_none() {
                        self.client = Some(ClientStats::new(pkt, &tls, store_pkt_num)?);
                    }
                }
                SERVER_HELLO => {
                    debug_assert_eq!(
                        tls_handshake_type.display(),
                        "Handshake Type: Server Hello (2)"
                    );
                    // We only need data from a single TLS Server Hello packet per stream.
                    if self.server.is_none() {
                        self.server = ServerStats::try_new(pkt, &tls, store_pkt_num)?;
                    }
                }
                CERTIFICATE => {
                    debug_assert_eq!(
                        tls_handshake_type.display(),
                        "Handshake Type: Certificate (11)"
                    );

                    let mut recs = Vec::new();
                    for hexdump in tls.values("tls.handshake.certificate") {
                        let der = hexdump
                            .split(':')
                            .map(|s| u8::from_str_radix(s, 16).map_err(|e| e.into()))
                            .collect::<Result<Vec<_>>>()?;
                        let (rem, x509) = X509Certificate::from_der(&der)?;
                        debug_assert!(rem.is_empty());
                        recs.push(ja4x::X509Rec::from(x509));
                    }
                    debug_assert!(!recs.is_empty());

                    self.x509.push(X509Stats {
                        packet: store_pkt_num.then_some(pkt.num),
                        recs,
                    });
                }
                _ => {}
            }
        }
        Ok(())
    }

    pub(crate) fn into_out(self, flags: FormatFlags) -> Option<OutStream> {
        let Stream {
            client,
            server,
            x509,
        } = self;

        if client.is_none() && server.is_none() && x509.is_empty() {
            None
        } else {
            Some(OutStream {
                client: client.map(|x| x.into_out(flags)),
                server: server.map(|x| x.into_out(flags)),
                tls_certs: x509
                    .into_iter()
                    .map(|x| x.into_out(flags.with_raw))
                    .collect(),
            })
        }
    }
}

#[derive(Debug, Serialize)]
pub(crate) struct OutStream {
    #[serde(flatten, skip_serializing_if = "Option::is_none")]
    client: Option<OutClient>,
    #[serde(flatten, skip_serializing_if = "Option::is_none")]
    server: Option<OutServer>,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    tls_certs: Vec<OutX509>,
}

/// X.509 certificates collected from a single packet.
#[derive(Debug)]
pub(crate) struct X509Stats {
    packet: Option<PacketNum>,
    recs: Vec<ja4x::X509Rec>,
}

impl X509Stats {
    fn into_out(self, with_raw: bool) -> OutX509 {
        let X509Stats { packet, recs } = self;
        let x509 = recs.into_iter().map(|x| x.into_out(with_raw)).collect();
        OutX509 {
            pkt_x509: packet,
            x509,
        }
    }
}

#[derive(Debug, Serialize)]
pub(crate) struct OutX509 {
    #[serde(skip_serializing_if = "Option::is_none")]
    pkt_x509: Option<PacketNum>,
    x509: Vec<ja4x::OutX509Rec>,
}

/// Information obtained from a TLS Client Hello packet.
#[derive(Debug)]
#[cfg_attr(test, derive(Clone))]
pub(crate) struct ClientStats {
    packet: Option<PacketNum>,
    tls_ver: TlsVersion,
    ciphers: Vec<String>,
    exts: Vec<u16>,
    /// Server Name Indication (SNI)
    sni: Option<String>,
    alpn: (Option<char>, Option<char>),
    sig_hash_algs: Vec<String>,
}

impl ClientStats {
    fn new(pkt: &Packet, tls: &Proto, store_pkt_num: bool) -> Result<Self> {
        let exts = tls_extensions_client(tls);
        let tls_ver = TlsVersion::new(tls, exts.contains(&TLS_EXT_SUPPORTED_VERSIONS))?;
        let sni = tls
            .first("tls.handshake.extensions_server_name")
            .ok()
            .map(str::to_owned);
        let alpn = tls
            .first("tls.handshake.extensions_alpn_str")
            .map_or((None, None), first_last);
        let ciphers = tls
            .values("tls.handshake.ciphersuite")
            .filter(|v| !TLS_GREASE_VALUES_STR.contains(v))
            .filter_map(|v| {
                let s = v.strip_prefix("0x");
                if s.is_none() {
                    debug!(cipher = v, %pkt.num, "Invalid cipher suite");
                }
                s.map(str::to_owned)
            })
            .collect();

        Ok(Self {
            packet: store_pkt_num.then_some(pkt.num),
            tls_ver,
            ciphers,
            exts,
            sni,
            alpn,
            sig_hash_algs: sig_hash_algs(pkt, tls),
        })
    }

    fn into_out(mut self, flags: FormatFlags) -> OutClient {
        let FormatFlags {
            with_raw,
            original_order,
        } = flags;

        let sni = self.sni.take();
        let pkt_ja4 = self.packet.take();
        let parts = PartsOfClientFingerprint::from_client_stats(self, original_order);

        let ja4 = {
            let s = parts.as_hashed_fingerprint();
            if original_order {
                Ja4Fingerprint::Unsorted(s)
            } else {
                Ja4Fingerprint::Sorted(s)
            }
        };
        let ja4_r = with_raw.then(|| {
            let s = parts.as_raw_fingerprint();
            if original_order {
                Ja4RawFingerprint::Unsorted(s)
            } else {
                Ja4RawFingerprint::Sorted(s)
            }
        });
        OutClient {
            sni,
            pkt_ja4,
            ja4,
            ja4_r,
        }
    }
}

/// Returns hex values of the signature algorithms.
fn sig_hash_algs(pkt: &Packet, tls: &Proto) -> Vec<String> {
    assert_eq!(tls.name(), "tls");

    // `signature_algorithms` is not the only TLS extension that contains
    // `tls.handshake.sig_hash_alg` fields. For example, `delegated_credentials`
    // contains them too; see https://github.com/FoxIO-LLC/ja4/issues/41
    //
    // We are only interested in `signature_algorithms` extension, so we skip forward
    // to it.
    let mut iter = tls
        .iter()
        .skip_while(|&md| md.name() != "tls.handshake.extension.type" || md.value() != "13");
    match iter.next() {
        Some(md) => debug_assert_eq!(md.display(), "Type: signature_algorithms (13)"),
        None => {
            debug!(%pkt.num, "signature_algorithms TLS extension not found");
            return Vec::new();
        }
    }
    match iter.next() {
        Some(md) => debug_assert_eq!(md.name(), "tls.handshake.extension.len"),
        None => {
            warn!(%pkt.num, "Unexpected end of TLS dissection");
            return Vec::new();
        }
    }

    iter.take_while(|&md| md.name().starts_with("tls.handshake.sig_hash_"))
        .filter(|&md| md.name() == "tls.handshake.sig_hash_alg")
        .filter_map(|md| {
            let s = md.value().strip_prefix("0x");
            if s.is_none() {
                warn!(%pkt.num, ?md, "signature algorithm value doesn't start with \"0x\"");
            }
            s.map(str::to_owned)
        })
        .collect()
}

/// Pieces of data that is used to construct [`Ja4Fingerprint`] and [`Ja4RawFingerprint`].
#[derive(Debug)]
struct PartsOfClientFingerprint {
    /// Leading part the JA4 fingerprint up to the first underscore, not including it.
    first_chunk: String,
    /// Comma-separated list of ciphers. The list is sorted iff `original_order`
    /// is `false`.
    ciphers: String,
    /// `"{exts}{opt_underscore}{sigs}"`, where `exts` ia a comma-separated list of
    /// extensions, excluding SNI and ALPN, `sigs` ia a comma-separated list of signature
    /// algorithms, and `opt_underscore` is an empty string if `sigs` is empty, otherwise
    /// it is an underscore. `exts` is sorted iff `original_order` is `false`. `sigs` is
    /// not sorted.
    exts_sigs: String,
}

impl PartsOfClientFingerprint {
    fn from_client_stats(stats: ClientStats, original_order: bool) -> Self {
        let ClientStats {
            packet,
            tls_ver,
            mut ciphers,
            mut exts,
            sni,
            alpn,
            sig_hash_algs,
        } = stats;
        // We've taken these out in `ClientStats::into_out`.
        assert!(packet.is_none() && sni.is_none());

        let quic = quic_marker(exts.contains(&TLS_EXT_QUIC_TRANSPORT_PARAMETERS));
        let sni_marker = if exts.contains(&TLS_EXT_SERVER_NAME) {
            'd'
        } else {
            'i'
        };

        let nr_ciphers = 99.min(ciphers.len());
        let nr_exts = 99.min(exts.len());
        if !original_order {
            exts.retain(|&v| v != TLS_EXT_SERVER_NAME && v != TLS_EXT_ALPN);
        }

        let first_chunk = format!(
            "{quic}{tls_ver}{sni_marker}{nr_ciphers:02}{nr_exts:02}{alpn_0}{alpn_1}",
            alpn_0 = alpn.0.unwrap_or('0'),
            alpn_1 = alpn.1.unwrap_or('0'),
        );

        if !original_order {
            ciphers.sort_unstable();
            exts.sort_unstable();
        }
        let ciphers = ciphers.join(",");
        let exts = exts.into_iter().map(|v| format!("{v:04x}")).join(",");

        let sigs = sig_hash_algs.join(",");
        // According to the specification, "if there are no signature algorithms in the
        // Hello packet, then the string ends without an underscore".
        let opt_underscore = if sigs.is_empty() { "" } else { "_" };

        Self {
            first_chunk,
            ciphers,
            exts_sigs: format!("{exts}{opt_underscore}{sigs}"),
        }
    }

    fn as_hashed_fingerprint(&self) -> String {
        let Self {
            first_chunk,
            ciphers,
            exts_sigs,
        } = self;
        let ciphers = crate::hash12(ciphers);
        let exts_sigs = crate::hash12(exts_sigs);
        format!("{first_chunk}_{ciphers}_{exts_sigs}")
    }

    fn as_raw_fingerprint(&self) -> String {
        let Self {
            first_chunk,
            ciphers,
            exts_sigs,
        } = self;
        format!("{first_chunk}_{ciphers}_{exts_sigs}")
    }
}

#[derive(Debug, Serialize)]
struct OutClient {
    /// Server Name Indication (SNI), obtained from the TLS Client Hello packet.
    #[serde(rename = "tls_server_name", skip_serializing_if = "Option::is_none")]
    sni: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pkt_ja4: Option<PacketNum>,
    #[serde(flatten)]
    ja4: Ja4Fingerprint,
    #[serde(flatten, skip_serializing_if = "Option::is_none")]
    ja4_r: Option<Ja4RawFingerprint>,
}

#[derive(Debug, Serialize)]
enum Ja4Fingerprint {
    #[serde(rename = "ja4")]
    Sorted(String),
    #[serde(rename = "ja4_o")]
    Unsorted(String),
}

#[derive(Debug, Serialize)]
enum Ja4RawFingerprint {
    #[serde(rename = "ja4_r")]
    Sorted(String),
    #[serde(rename = "ja4_ro")]
    Unsorted(String),
}

/// Information obtained from a TLS Server Hello packet.
#[derive(Debug)]
#[cfg_attr(test, derive(Clone))]
pub(crate) struct ServerStats {
    packet: Option<PacketNum>,
    is_quic: bool,
    tls_ver: TlsVersion,
    cipher: String,
    exts: Vec<u16>,
    alpn: (Option<char>, Option<char>),
}

impl ServerStats {
    fn try_new(pkt: &Packet, tls: &Proto, store_pkt_num: bool) -> Result<Option<Self>> {
        let exts = tls_extensions_server(tls);
        let tls_ver = TlsVersion::new(tls, exts.contains(&TLS_EXT_SUPPORTED_VERSIONS))?;
        let alpn = tls
            .first("tls.handshake.extensions_alpn_str")
            .map_or((None, None), first_last);

        let v = tls.first("tls.handshake.ciphersuite")?;
        let Some(cipher) = v.strip_prefix("0x") else {
            debug!(cipher = v, %pkt.num, "Invalid cipher suite");
            return Ok(None);
        };

        Ok(Some(Self {
            packet: store_pkt_num.then_some(pkt.num),
            is_quic: pkt.find_proto("udp").is_some(),
            tls_ver,
            cipher: cipher.to_owned(),
            exts,
            alpn,
        }))
    }

    fn into_out(self, flags: FormatFlags) -> OutServer {
        let Self {
            packet,
            is_quic,
            tls_ver,
            cipher,
            exts,
            alpn,
        } = self;

        let quic = quic_marker(is_quic);
        let nr_exts = 99.min(exts.len());

        let two_chunks = format!(
            "{quic}{tls_ver}{nr_exts:02}{alpn_0}{alpn_1}_{cipher}",
            alpn_0 = alpn.0.unwrap_or('0'),
            alpn_1 = alpn.1.unwrap_or('0'),
        );

        // Note that we are preserving the original order of server's TLS extensions.
        let exts = exts.into_iter().map(|v| format!("{v:04x}")).join(",");

        OutServer {
            pkt_ja4s: packet,
            ja4s: format!("{two_chunks}_{hash}", hash = crate::hash12(&exts)),
            ja4s_r: flags.with_raw.then(|| format!("{two_chunks}_{exts}")),
        }
    }
}

#[derive(Debug, Serialize)]
struct OutServer {
    #[serde(skip_serializing_if = "Option::is_none")]
    pkt_ja4s: Option<PacketNum>,
    ja4s: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    ja4s_r: Option<String>,
}

fn quic_marker(is_quic: bool) -> char {
    if is_quic {
        'q'
    } else {
        't'
    }
}

// See https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml#tls-extensiontype-values-1
const TLS_EXT_SERVER_NAME: u16 = 0; // Server Name Indication (SNI)
const TLS_EXT_ALPN: u16 = 16; // Application-Layer Protocol Negotiation (ALPN)
const TLS_EXT_SUPPORTED_VERSIONS: u16 = 43;
const TLS_EXT_QUIC_TRANSPORT_PARAMETERS: u16 = 57;

#[derive(Debug, Clone, PartialEq, Eq)]
enum TlsVersion {
    /// TLS 1.3
    Tls1_3,
    /// TLS 1.2
    Tls1_2,
    /// TLS 1.1
    Tls1_1,
    /// TLS 1.0
    Tls1_0,
    /// SSL 3.0
    Ssl3_0,
    /// SSL 2.0
    Ssl2_0,
    Unknown(String),
}

impl From<&str> for TlsVersion {
    fn from(s: &str) -> Self {
        match s {
            "0x0304" => TlsVersion::Tls1_3,
            "0x0303" => TlsVersion::Tls1_2,
            "0x0302" => TlsVersion::Tls1_1,
            "0x0301" => TlsVersion::Tls1_0,
            "0x0300" => TlsVersion::Ssl3_0,
            "0x0002" => TlsVersion::Ssl2_0,
            _ => TlsVersion::Unknown(s.to_owned()),
        }
    }
}

impl fmt::Display for TlsVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let s = match self {
            TlsVersion::Tls1_3 => "13",
            TlsVersion::Tls1_2 => "12",
            TlsVersion::Tls1_1 => "11",
            TlsVersion::Tls1_0 => "10",
            TlsVersion::Ssl3_0 => "s3",
            TlsVersion::Ssl2_0 => "s2",
            TlsVersion::Unknown(_) => "00",
        };
        write!(f, "{s}")
    }
}

#[test]
fn test_tls_version() {
    assert_eq!(TlsVersion::from("0x0304"), TlsVersion::Tls1_3);
    assert_eq!(
        TlsVersion::from("spam"),
        TlsVersion::Unknown("spam".to_owned())
    );

    assert_eq!(TlsVersion::Tls1_2.to_string(), "12");
    assert_eq!(TlsVersion::Unknown("origins".to_owned()).to_string(), "00");
}

impl TlsVersion {
    fn new(tls: &Proto, supported_versions_p: bool) -> Result<Self> {
        let tls_version = if !supported_versions_p {
            // Not to be confused with "tls.record.version".
            tls.first("tls.handshake.version")?
        } else {
            tls.values("tls.handshake.extensions.supported_version")
                .filter(|v| !TLS_GREASE_VALUES_STR.contains(v))
                .sorted_unstable()
                .next_back()
                .ok_or_else(|| Error::MissingField {
                    name: "tls.handshake.extensions.supported_version".to_owned(),
                })?
        };
        Ok(tls_version.into())
    }
}

/// See <https://datatracker.ietf.org/doc/html/draft-davidben-tls-grease-01#page-5>
const TLS_GREASE_VALUES_STR: [&str; 16] = [
    "0x0a0a", "0x1a1a", "0x2a2a", "0x3a3a", "0x4a4a", "0x5a5a", "0x6a6a", "0x7a7a", "0x8a8a",
    "0x9a9a", "0xaaaa", "0xbaba", "0xcaca", "0xdada", "0xeaea", "0xfafa",
];
const TLS_GREASE_VALUES_INT: [u16; 16] = [
    0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a, 0x8a8a, 0x9a9a, 0xaaaa, 0xbaba,
    0xcaca, 0xdada, 0xeaea, 0xfafa,
];

/// Returns [TLS extension type values], excluding [GREASE][`TLS_GREASE_VALUES_INT`],
/// in the order of their appearance in the packet.
///
/// [TLS extension type values]: https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml#tls-extensiontype-values-1
fn tls_extensions_client(tls: &Proto) -> Vec<u16> {
    assert_eq!(tls.name(), "tls");

    tls.fields("tls.handshake.extension.type").filter_map(|md| {
        match md.value().parse() {
            Ok(n) if TLS_GREASE_VALUES_INT.contains(&n) => None,
            Ok(n) => Some(n),
            Err(error) => {
                debug!(packet = %tls.packet_num, value = md.value(), showname = md.display(), %error, "Invalid TLS extension");
                None
            }
        }
    })
    .collect()
}

/// Returns [TLS extension type values] in the order of their appearance in the packet.
///
/// [TLS extension type values]: https://www.iana.org/assignments/tls-extensiontype-values/tls-extensiontype-values.xhtml#tls-extensiontype-values-1
fn tls_extensions_server(tls: &Proto) -> Vec<u16> {
    assert_eq!(tls.name(), "tls");

    tls.fields("tls.handshake.extension.type").filter_map(|md| {
        md.value().parse::<u16>().map_err(|e| {
            debug!(packet = %tls.packet_num, value = md.value(), showname = md.display(), error = %e, "Invalid TLS extension");
        }).ok()
    })
    .collect()
}

fn first_last(s: &str) -> (Option<char>, Option<char>) {
    let replace_nonascii_with_9 = |c: char| {
        if c.is_ascii() {
            c
        } else {
            '9'
        }
    };
    let mut chars = s.chars();
    let first = chars.next().map(replace_nonascii_with_9);
    let last = chars.next_back().map(replace_nonascii_with_9);
    (first, last)
}

#[test]
fn test_first_last() {
    assert_eq!(first_last(""), (None, None));
    assert_eq!(first_last("a"), (Some('a'), None));
    assert_eq!(first_last("ab"), (Some('a'), Some('b')));
    assert_eq!(first_last("abc"), (Some('a'), Some('c')));
}

#[test]
fn test_first_last_non_ascii() {
    assert_eq!('�', char::REPLACEMENT_CHARACTER);
    assert_eq!(first_last("�"), (Some('9'), None));
    assert_eq!(first_last("��"), (Some('9'), Some('9')));
    assert_eq!(first_last("�x�"), (Some('9'), Some('9')));
    assert_eq!(first_last("x�"), (Some('x'), Some('9')));
    assert_eq!(first_last("�x"), (Some('9'), Some('x')));
}

#[cfg(test)]
mod tests {
    use super::*;
    use expect_test::expect;

    #[test]
    fn test_client_stats_into_out() {
        let ciphers = [
            "1301", "1302", "1303", "c02b", "c02f", "c02c", "c030", "cca9", "cca8", "c013", "c014",
            "009c", "009d", "002f", "0035",
        ]
        .into_iter()
        .map(str::to_owned)
        .collect::<Vec<_>>();

        let exts = vec![
            0x001b, 0x0000, 0x0033, 0x0010, 0x4469, 0x0017, 0x002d, 0x000d, 0x0005, 0x0023, 0x0012,
            0x002b, 0xff01, 0x000b, 0x000a, 0x0015,
        ];

        let sig_hash_algs = [
            "0403", "0804", "0401", "0503", "0805", "0501", "0806", "0601",
        ]
        .into_iter()
        .map(str::to_owned)
        .collect::<Vec<_>>();

        let stats = ClientStats {
            packet: None,
            tls_ver: TlsVersion::Tls1_3,
            ciphers,
            exts,
            sni: Some("example.com".to_owned()),
            alpn: (Some('h'), Some('2')),
            sig_hash_algs,
        };

        let out = stats.clone().into_out(FormatFlags::default());
        expect![[r#"
            {
              "tls_server_name": "example.com",
              "ja4": "t13d1516h2_8daaf6152771_e5627efa2ab1"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let out = stats.clone().into_out(FormatFlags {
            with_raw: true,
            ..Default::default()
        });
        expect![[r#"
            {
              "tls_server_name": "example.com",
              "ja4": "t13d1516h2_8daaf6152771_e5627efa2ab1",
              "ja4_r": "t13d1516h2_002f,0035,009c,009d,1301,1302,1303,c013,c014,c02b,c02c,c02f,c030,cca8,cca9_0005,000a,000b,000d,0012,0015,0017,001b,0023,002b,002d,0033,4469,ff01_0403,0804,0401,0503,0805,0501,0806,0601"
            }"#]].assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let out = stats.clone().into_out(FormatFlags {
            original_order: true,
            ..Default::default()
        });
        expect![[r#"
            {
              "tls_server_name": "example.com",
              "ja4_o": "t13d1516h2_acb858a92679_18f69afefd3d"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let out = stats.clone().into_out(FormatFlags {
            with_raw: true,
            original_order: true,
        });
        expect![[r#"
            {
              "tls_server_name": "example.com",
              "ja4_o": "t13d1516h2_acb858a92679_18f69afefd3d",
              "ja4_ro": "t13d1516h2_1301,1302,1303,c02b,c02f,c02c,c030,cca9,cca8,c013,c014,009c,009d,002f,0035_001b,0000,0033,0010,4469,0017,002d,000d,0005,0023,0012,002b,ff01,000b,000a,0015_0403,0804,0401,0503,0805,0501,0806,0601"
            }"#]].assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let stats = ClientStats {
            packet: Some(PacketNum(10)),
            ..stats
        };
        let out = stats.into_out(FormatFlags::default());
        expect![[r#"
            {
              "tls_server_name": "example.com",
              "pkt_ja4": 10,
              "ja4": "t13d1516h2_8daaf6152771_e5627efa2ab1"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());
    }

    #[test]
    fn test_server_stats_into_out() {
        let stats = ServerStats {
            packet: None,
            is_quic: false,
            tls_ver: TlsVersion::Tls1_2,
            cipher: "c030".to_owned(),
            exts: vec![0x0005, 0x0017, 0xff01, 0x0000],
            alpn: (None, None),
        };

        let out = stats.clone().into_out(FormatFlags::default());
        expect![[r#"
            {
              "ja4s": "t120400_c030_4e8089b08790"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let out = stats.clone().into_out(FormatFlags {
            with_raw: true,
            ..Default::default()
        });
        expect![[r#"
            {
              "ja4s": "t120400_c030_4e8089b08790",
              "ja4s_r": "t120400_c030_0005,0017,ff01,0000"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let stats = ServerStats {
            packet: Some(PacketNum(16)),
            ..stats
        };
        let out = stats.into_out(FormatFlags::default());
        expect![[r#"
            {
              "pkt_ja4s": 16,
              "ja4s": "t120400_c030_4e8089b08790"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());
    }
}
