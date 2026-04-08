// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

use std::io;

use clap::Parser as _;
use color_eyre::eyre;
use tracing_subscriber::filter::EnvFilter;

fn main() -> eyre::Result<()> {
    tracing_subscriber::fmt()
        .with_env_filter(EnvFilter::from_default_env())
        .with_file(true)
        .with_line_number(true)
        .init();
    color_eyre::install()?;

    match ja4::Cli::parse().run(&mut io::stdout()) {
        Err(ja4::Error::Io(e)) if matches!(e.kind(), io::ErrorKind::BrokenPipe) => Ok(()),
        Err(e) => Err(e.into()),
        Ok(()) => Ok(()),
    }
}
