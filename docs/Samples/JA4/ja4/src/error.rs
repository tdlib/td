// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

use std::{io, path::PathBuf};

use ja4x::x509_parser;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("unable to run 'tshark': {source}")]
    TsharkNotFound { source: io::Error },
    #[error("failed to parse `tshark --version` output")]
    ParseTsharkVersion,
    #[error("failed to parse tshark version: {0}")]
    ParseTsharkSemver(#[from] semver::Error),
    #[error("IO error: {0}")]
    Io(#[from] io::Error),
    #[error("path contains non-UTF-8 characters: {0:?}")]
    NonUtf8Path(PathBuf),
    #[error("failed to load configuration: {0}")]
    Config(#[from] config::ConfigError),
    #[error("none of fingerprints is enabled; check config.toml and environment")]
    VoidConf,
    #[error("'{name}' is missing")]
    MissingField { name: String },
    #[error("packet timestamp is missing")]
    MissingTimestamp,
    #[error("integer expected, got {0}")]
    ParseInt(#[from] std::num::ParseIntError),
    #[error("invalid value of http.request.{field}: {value}")]
    InvalidHttpRequest { field: String, value: String },
    #[error("JSON error: {0}")]
    Json(#[from] serde_json::Error),
    #[error("YAML error: {0}")]
    Yaml(#[from] serde_yaml::Error),
    #[error("failed to parse tls.handshake.certificate: {0}")]
    X509(#[from] x509_parser::nom::Err<x509_parser::error::X509Error>),
}
