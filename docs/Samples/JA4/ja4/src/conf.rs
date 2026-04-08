// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

use fs_err::File;
use serde::Deserialize;

use std::{env, io::Write as _, path::PathBuf};

#[derive(Debug, Deserialize)]
pub(crate) struct Conf {
    pub(crate) http: ConfBasic,
    pub(crate) ssh: ConfSsh,
    pub(crate) time: ConfBasic,
    pub(crate) tls: ConfBasic,
}

#[derive(Debug, Deserialize)]
pub(crate) struct ConfBasic {
    pub(crate) enabled: bool,
}

#[derive(Debug, Deserialize)]
pub(crate) struct ConfSsh {
    pub(crate) enabled: bool,
    /// JA4SSH (SSH traffic fingerprinting) runs every `sample_size` packets
    /// per SSH TCP stream.
    pub(crate) sample_size: usize,
}

impl ConfSsh {
    const DEFAULT_SAMPLE_SIZE: usize = 200;

    fn prepare(mut self) -> Self {
        if self.enabled && self.sample_size == 0 {
            tracing::warn!(
                "ssh.sample_size is 0, setting to {}",
                Self::DEFAULT_SAMPLE_SIZE
            );
            self.sample_size = Self::DEFAULT_SAMPLE_SIZE;
        }
        self
    }
}

impl Conf {
    pub(crate) fn load() -> crate::Result<Self> {
        let config_dir = config_dir();
        let config_file = config_dir.join("config.toml");

        // Configuration sources precedence:
        // environment variables > config file > defaults

        let config_builder = config::Config::builder()
            .set_default("http.enabled", true)?
            .set_default("ssh.enabled", true)?
            .set_default("ssh.sample_size", 200)?
            .set_default("time.enabled", true)?
            .set_default("tls.enabled", true)?;

        if !config_file.exists() {
            let example_config = include_bytes!("../config.toml");
            fs_err::create_dir_all(&config_dir).map_err(|e| {
                tracing::error!(error = %e, ?config_dir, "cannot create directory");
                e
            })?;
            let mut file = File::create(&config_file).map_err(|e| {
                tracing::error!(error = %e, "could not create config file");
                e
            })?;
            file.write_all(example_config).map_err(|e| {
                tracing::error!(error = %e, "could not write default config file");
                e
            })?;
        }

        let config = config_builder
            .add_source(config::File::from(config_file))
            .add_source(
                config::Environment::with_prefix("JA4")
                    .prefix_separator("_")
                    .separator("__"),
            )
            .build()?;

        let conf = config.try_deserialize::<Conf>()?.prepare();

        if conf.http.enabled || conf.time.enabled || conf.ssh.enabled || conf.tls.enabled {
            Ok(conf)
        } else {
            Err(crate::Error::VoidConf)
        }
    }

    fn prepare(mut self) -> Self {
        self.ssh = self.ssh.prepare();
        self
    }
}

#[cfg(not(target_os = "windows"))]
fn home_dir() -> PathBuf {
    env::var("HOME").expect("$HOME not found").into()
}

#[cfg(target_os = "windows")]
fn home_dir() -> PathBuf {
    env::var("USERPROFILE")
        .expect("%userprofile% not found")
        .into()
}

fn config_dir() -> PathBuf {
    let config_dir =
        env::var("XDG_CONFIG_HOME").map_or_else(|_| home_dir().join(".config"), PathBuf::from);
    config_dir.join("ja4")
}
