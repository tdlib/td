// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

//! Capture file abstractions

use std::fmt;

use serde::Serialize;

use crate::{Error, Result};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize)]
pub(crate) struct PacketNum(pub(crate) usize);

impl fmt::Display for PacketNum {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

#[derive(Clone)]
pub(crate) struct Packet<'a> {
    inner: &'a rtshark::Packet,
    /// Sequential number of this packet in the capture file.
    pub(crate) num: PacketNum,
}

impl<'a> Packet<'a> {
    pub(crate) fn new(inner: &'a rtshark::Packet, num: usize) -> Self {
        Self {
            inner,
            num: PacketNum(num),
        }
    }

    /// Returns an iterator over the [protocols][Proto] with the given name.
    pub(crate) fn protos<'b>(&'b self, name: &'b str) -> impl Iterator<Item = Proto<'b>> {
        self.inner
            .iter()
            .filter(move |layer| layer.name() == name)
            .map(|layer| Proto {
                inner: layer,
                packet_num: self.num,
            })
    }

    /// Gets the first protocol with the given name.
    pub(crate) fn find_proto(&self, name: &str) -> Option<Proto<'a>> {
        self.inner.layer_name(name).map(|inner| Proto {
            inner,
            packet_num: self.num,
        })
    }

    // XXX-TODO(vvv): Propose to change the type of `rtshark::Packet::timestamp_micros`
    // to `Option<u64>` (*unsigned*).
    pub(crate) fn timestamp_micros(&self) -> Result<i64> {
        self.inner.timestamp_micros().ok_or(Error::MissingTimestamp)
    }

    /// Returns an iterator over the [`Proto`]cols of this packet.
    pub(crate) fn iter(&self) -> impl Iterator<Item = Proto<'_>> {
        self.inner.iter().map(|layer| Proto {
            inner: layer,
            packet_num: self.num,
        })
    }
}

#[derive(Clone)]
pub(crate) struct Proto<'a> {
    inner: &'a rtshark::Layer,
    pub(crate) packet_num: PacketNum,
}

impl Proto<'_> {
    /// Returns the name of the underlying [`rtshark::Layer`], i.e., the name of the protocol
    /// returned by `tshark`.
    pub(crate) fn name(&self) -> &str {
        self.inner.name()
    }

    /// Returns an iterator over all [`rtshark::Metadata`] for this protocol.
    pub(crate) fn iter(&self) -> impl Iterator<Item = &rtshark::Metadata> {
        self.inner.iter()
    }

    /// Returns an iterator over the sequence of [`rtshark::Metadata`] with the given [name].
    ///
    /// [name]: rtshark::Metadata::name
    pub(crate) fn fields<'a>(
        &'a self,
        name: &'a str,
    ) -> impl Iterator<Item = &'a rtshark::Metadata> {
        self.inner.iter().filter(move |md| md.name() == name)
    }

    /// Returns the [values] of [`rtshark::Metadata`] with the given [name].
    ///
    /// [name]: rtshark::Metadata::name
    /// [values]: rtshark::Metadata::value
    pub(crate) fn values<'a>(&'a self, name: &'a str) -> impl Iterator<Item = &'a str> {
        self.fields(name).map(|md| md.value())
    }

    /// Returns the first field ([`rtshark::Metadata`]) with the given name.
    pub(crate) fn find(&self, name: &str) -> Result<&rtshark::Metadata> {
        assert!(name.starts_with(self.name()));
        self.inner
            .metadata(name)
            .ok_or_else(|| Error::MissingField {
                name: name.to_owned(),
            })
    }

    /// Returns the [value] of the first field ([`rtshark::Metadata`]) with the given name.
    ///
    /// [value]: rtshark::Metadata::value
    pub(crate) fn first(&self, name: &str) -> Result<&str> {
        self.find(name).map(|md| md.value())
    }
}
