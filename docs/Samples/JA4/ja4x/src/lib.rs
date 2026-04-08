// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

pub use x509_parser;

use indexmap::IndexMap;
use itertools::Itertools as _;
use serde::Serialize;
use x509_parser::{certificate::X509Certificate, oid_registry::OidRegistry, x509};

#[derive(Debug, Serialize)]
pub struct OutX509Rec {
    ja4x: String,
    #[serde(skip_serializing_if = "Option::is_none")]
    ja4x_r: Option<String>,
    #[serde(flatten)]
    inner: IndexMap<String, String>,
}

/// The data obtained from X.509 certificate.
#[derive(Debug)]
pub struct X509Rec {
    /// Issuer RDNs (relative distinguished names).
    pub issuer_rdns: String,
    /// Subject RDNs.
    pub subject_rdns: String,
    pub extensions: String,
    /// Issuer OIDs.
    pub issuer: Vec<Oid>,
    /// Subject OIDs.
    pub subject: Vec<Oid>,
}

impl X509Rec {
    pub fn into_out(self, with_raw: bool) -> OutX509Rec {
        let X509Rec {
            issuer_rdns,
            subject_rdns,
            extensions,
            issuer,
            subject,
        } = self;

        let parts = [issuer_rdns, subject_rdns, extensions];
        let ja4x = parts.iter().map(hash12).join("_");
        let ja4x_r = with_raw.then(|| parts.join("_"));

        let issuer_items = issuer.into_iter().filter_map(|oid| oid.into_kv("issuer"));
        let subject_items = subject.into_iter().filter_map(|oid| oid.into_kv("subject"));

        OutX509Rec {
            ja4x,
            ja4x_r,
            inner: issuer_items.chain(subject_items).collect(),
        }
    }
}

impl From<X509Certificate<'_>> for X509Rec {
    fn from(x509: X509Certificate) -> Self {
        let issuer_rdns = x509
            .issuer()
            .iter_attributes()
            .map(|a| hex::encode(a.attr_type().as_bytes()))
            .join(",");

        let subject_rdns = x509
            .subject()
            .iter_attributes()
            .map(|a| hex::encode(a.attr_type().as_bytes()))
            .join(",");

        let extensions = x509
            .extensions()
            .iter()
            .map(|ext| hex::encode(ext.oid.as_bytes()))
            .join(",");

        let oid_reg = OidRegistry::default().with_x509();
        let issuer = x509
            .issuer()
            .iter_attributes()
            .map(|attr| Oid::new(attr, &oid_reg))
            .collect();
        let subject = x509
            .subject()
            .iter_attributes()
            .map(|attr| Oid::new(attr, &oid_reg))
            .collect();

        Self {
            issuer_rdns,
            subject_rdns,
            extensions,
            issuer,
            subject,
        }
    }
}

/// Object identifier representation.
#[derive(Debug, Clone)]
pub struct Oid {
    pub oid: String,
    pub short_name: Option<String>,
    pub value: Option<String>,
}

impl Oid {
    fn new(attr: &x509::AttributeTypeAndValue, oid_reg: &OidRegistry) -> Self {
        let oid = attr.attr_type();
        let entry = oid_reg.get(oid);
        let value = match attr.as_str() {
            Ok(s) => Some(s.to_owned()),
            Err(error) => {
                tracing::debug!(%error, ?attr, "the object doesn't contain a string value");
                None
            }
        };
        Self {
            oid: oid.to_string(),
            short_name: entry.map(|e| e.sn().to_owned()),
            value,
        }
    }

    fn into_kv(self, key_prefix: &str) -> Option<(String, String)> {
        let Self {
            oid: _,
            short_name,
            value,
        } = self;

        let short_name = short_name?;
        let value = value?;
        let mut chars = short_name.chars();
        let key: String = match chars.next() {
            None => return None,
            Some(first) => first.to_uppercase().chain(chars).collect(),
        };
        Some((format!("{key_prefix}{key}"), value))
    }
}

#[test]
fn test_oid_into_kv() {
    let oid = Oid {
        oid: "2.5.4.6".to_owned(),
        short_name: Some("countryName".to_owned()),
        value: Some("US".to_owned()),
    };
    assert_eq!(
        oid.clone().into_kv("issuer"),
        Some(("issuerCountryName".to_owned(), "US".to_owned()))
    );
    assert!(Oid { value: None, ..oid }.into_kv("issuer").is_none());
}

/// Returns first 12 characters of the SHA-256 hash of the given string.
///
/// Returns `"000000000000"` (12 zeros) if the input string is empty.
fn hash12(s: impl AsRef<str>) -> String {
    use sha2::{Digest as _, Sha256};

    let s = s.as_ref();
    if s.is_empty() {
        "000000000000".to_owned()
    } else {
        let sha256 = hex::encode(Sha256::digest(s));
        sha256[..12].into()
    }
}

#[test]
fn test_hash12() {
    assert_eq!(hash12("551d0f,551d25,551d11"), "aae71e8db6d7");
    assert_eq!(hash12(""), "000000000000");
}
