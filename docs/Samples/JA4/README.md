# JA4+ (Rust Implementation) <!-- omit from toc -->

This tool implements JA4+, a fingerprinting methodology for network traffic analysis. It processes PCAP files and extracts JA4+ fingerprints for multiple protocols, including TLS, HTTP, SSH, TCP, and X.509 certificates. The output is structured in YAML or JSON format, providing detailed metadata such as IP addresses, ports, and domain names. This tool is designed for security research, threat detection, and network traffic investigation.

For more details on JA4+ and its implementations in other open-source tools (Python, Wireshark, and Zeek), see the [main JA4+ README](../README.md).

## Table of Contents <!-- omit from toc -->

- [Dependencies](#dependencies)
  - [Installing tshark](#installing-tshark)
    - [Linux](#linux)
    - [macOS](#macos)
    - [Windows](#windows)
- [Binaries](#binaries)
  - [Release Assets](#release-assets)
- [Building](#building)
- [Running JA4+](#running-ja4)
  - [Usage](#usage)
    - [Command-line Arguments for `ja4`](#command-line-arguments-for-ja4)
    - [Command-line Arguments for `ja4x`](#command-line-arguments-for-ja4x)
  - [Examples](#examples)
    - [`ja4` output](#ja4-output)
    - [`ja4x` output](#ja4x-output)
  - [Using a Key File for TLS Decryption](#using-a-key-file-for-tls-decryption)
- [Testing](#testing)
- [Creating a Release](#creating-a-release)
- [License](#license)

## Dependencies

JA4+ requires `tshark` v4.0.6 or later for full functionality.

### Installing tshark

#### Linux

Install it using your package manager (the package name is either `tshark` or `wireshark-cli`, depending on the distribution). For example, on Ubuntu:

```sh
sudo apt install tshark
```

#### macOS

1. [Download](https://www.wireshark.org/download.html) and install Wireshark (includes `tshark`).
2. Add `tshark` to your `PATH`:
   ```sh
   sudo ln -s /Applications/Wireshark.app/Contents/MacOS/tshark /usr/local/bin/tshark
   ```

#### Windows

1. [Download](https://www.wireshark.org/download.html) and install Wireshark (includes `tshark.exe`).
2. Locate `tshark.exe` (usually in `C:\Program Files\Wireshark\tshark.exe`).
3. Add the folder containing `tshark.exe` to your system `PATH`:
   - Open **System Properties** > **Environment Variables** > **Edit Path**.

## Binaries

Download the latest JA4 binaries from the [Releases](https://github.com/FoxIO-LLC/ja4/releases) page. The release versions for the Rust implementation follow [Semantic Versioning](https://semver.org/) and are marked as `vX.Y.Z`, unlike Wireshark plugin releases.

### Release Assets

Release assets are named as follows:

- `ja4-vX.Y.Z-<architecture>-<platform>.tar.gz` (e.g., `ja4-v0.18.5-x86_64-unknown-linux-musl.tar.gz` for Linux, `ja4-v0.18.5-aarch64-apple-darwin.tar.gz` for macOS ARM64)

These files are attached to a release named like `rust-vX.Y.Z`. Choose the appropriate file for your system.

## Building

Ensure Rust and Cargo are installed via [Rustup](https://rustup.rs/) or your package manager (`sudo apt install rustup`, etc.).  

Build the binaries with:  

```sh
cargo build --release
```

You can find the `ja4` and `ja4x` binaries in `target/release/`.

## Running JA4+

### Usage

#### Command-line Arguments for `ja4`

```txt
Arguments:
  <PCAP>                           The capture file to process

Options:
  -j, --json                       JSON output (default is YAML)
  -r, --with-raw                   Include raw (unhashed) fingerprints in the output
  -O, --original-order             Preserve the original order of values
      --keylog-file <KEYLOG_FILE>  The key log file that enables decryption of TLS traffic
  -n, --with-packet-numbers        Include packet numbers (`pkt_*` fields) in the output
  -h, --help                       Print help (see more with '--help')
  -V, --version                    Print version
```

**Note:**

`--original-order` disables sorting of ciphers and TLS extensions for JA4 (TLS client) and disables sorting of headers and cookies for JA4H (HTTP client).

#### Command-line Arguments for `ja4x`

`ja4x` CLI utility reads X.509 certificate files, DER or PEM encoded, and prints JA4X fingerprints, Issuer, and Subject information.

```txt
Arguments:
  [CERTS]...  X.509 certificate(s) in DER or PEM format

Options:
  -j, --json      JSON output (default is YAML)
  -r, --with-raw  Include raw (unhashed) fingerprints in the output
  -h, --help      Print help
  -V, --version   Print version
```

### Examples

#### `ja4` output

Running `ja4 capturefile.pcapng` might produce output like this:

```yaml
- stream: 0
  transport: tcp
  src: 192.168.1.168
  dst: 142.251.16.94
  src_port: 50112
  dst_port: 443
  tls_server_name: clientservices.googleapis.com
  ja4: t13d1516h2_8daaf6152771_e5627efa2ab1
- stream: 1
  transport: tcp
  src: 192.168.1.168
  dst: 142.251.163.147
  src_port: 50113
  dst_port: 443
  tls_server_name: www.google.com
  ja4: t13d1516h2_8daaf6152771_e5627efa2ab1
```

#### `ja4x` output

```yaml
path: sample.pem
ja4x: a373a9f83c6b_2bab15409345_7bf9a7bf7029
issuerCountryName: US
issuerOrganizationName: DigiCert Inc
issuerCommonName: DigiCert TLS RSA SHA256 2020 CA1
subjectCountryName: US
subjectStateOrProvinceName: California
subjectLocalityName: San Francisco
subjectOrganizationName: Cisco OpenDNS LLC
subjectCommonName: api.opendns.com
```

### Using a Key File for TLS Decryption

The `--keylog-file` option lets `ja4` decrypt TLS traffic using a **key log file**, which contains session keys needed for decryption.

Key log files can be generated by **browsers** (e.g., Firefox, Chrome) or **servers** running OpenSSL-based software. The file must be captured during traffic recording for decryption to work.

Run `ja4` with a key file:

```sh
ja4 capturefile.pcapng --keylog-file sslkeylog.log
```

For details on generating an SSL key log file, see:  
[Wireshark Wiki: Using the (Pre)-Master-Secret Log File](https://wiki.wireshark.org/TLS#using-the-pre-master-secret)

**Note:**

- Works for TLS 1.3 only with session keys; PFS may prevent decryption.
- You can embed the TLS key log file in a capture file: `editcap --inject-secrets tls,keys.txt in.pcap out-dsb.pcapng`

## Testing

Sample PCAP files for testing `ja4` are available in the [`pcap`](../pcap/) directory. These files cover various network protocols and scenarios, including TLS, QUIC, HTTP, SSH, and edge cases. They can be used to verify expected output and assess fingerprinting accuracy.

Run automated tests with:

```sh
cargo test
```

## Creating a Release

To create a Rust release, push a tag starting with `rust-`, for example:

```sh
git tag rust-v0.18.5
git push origin rust-v0.18.5
```

## License

See the [Licensing](../README.md#licensing) section in the repo root. We are committed to work with vendors and open source projects to help implement JA4+ into those tools. Please contact john@foxio.io with any questions.

Copyright (c) 2024, FoxIO
