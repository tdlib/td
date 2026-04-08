# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

<!--
  -- ## Types of changes
  --
  -- * `Added` for new features.
  -- * `Changed` for changes in existing functionality.
  -- * `Deprecated` for soon-to-be removed features.
  -- * `Removed` for now removed features.
  -- * `Fixed` for any bug fixes.
  -- * `Security` in case of vulnerabilities.
  -->

## [Unreleased]

## [0.18.5] - 2025-01-17

- Update dependencies.

## [0.18.4] - 2024-09-30

- Update the `release` CI workflow to generate macOS binaries of the Rust apps.

## [0.18.3] - 2024-09-10

### Fixed

- Fix parsing of `tshark --version` output.

## [0.18.2] - 2024-05-22

### Fixed

- `cargo update` the dependencies. This fixes compilation error of `time` on nightly.

## [0.18.1] - 2024-02-04

### Fixed

- JA4H: Sort cookie-pairs properly (#58).

## [0.18.0] - 2024-02-04

### Fixed

- Generate a JA4SSH fingerprint every 200 *SSH* (layer 7) packets.
- Fix calculation of JA4H\_c (#58).

## [0.17.0] - 2024-01-31

### Fixed

- JA4SSH (mode of TCP payload length): Handle collisions, resolve nondeterminism (#51).
- JA4L: Fix a panic that only reproduced in dev mode (#51).
- Fix processing of GRE tunneling traffic (#51).
- Skip packets with "icmpv6" layer (#51).

### Changed

- ja4: Improve debug logging.
- ja4x: Provide more context in the error message (#52).

## [0.16.2] - 2024-01-04

### Fixed

- JA4: Include SNI (0000) and ALPN (0010) in the "original" outputs (#40).
- JA4H: Search for "Cookie" and "Referer" fields in a case-insensitive fashion.
- JA4: Take signature algorithm hex values from `signature_algorithms` extension only (#41).

## [0.16.1] - 2023-12-22

### Fixed

- JA4SSH: When counting ACK packets, look for bare ACK flags only, skipping SYN-ACK,
  PSH-ACK, FIN-ACK, etc. (#36)

## [0.16.0] - 2023-12-12

### Changed

- Handle non-ASCII ALPN strings (#16).

### Fixed

- Support tshark v4.2.0.

## [0.15.2] - 2023-11-09

### Fixed

- Ignore extraneous TCP flags when choosing packets for JA4L calculation (#22).

## [0.15.1] - 2023-10-12

### Fixed

- Don't skip X.509 certificates contained in "Server Hello" TLS packets.

## [0.15.0] - 2023-10-08

### Added

- Add capture files and expected output.

## [0.14.0] - 2023-10-04

### Added

- Add Rust sources of `ja4` and `ja4x` CLI tools.

<!-- Links -->

[unreleased]: https://github.com/FoxIO-LLC/ja4/compare/v0.18.5...HEAD
[0.18.5]: https://github.com/FoxIO-LLC/ja4/compare/v0.18.4...v0.18.5
[0.18.4]: https://github.com/FoxIO-LLC/ja4/compare/v0.18.3...v0.18.4
[0.18.3]: https://github.com/FoxIO-LLC/ja4/compare/v0.18.2...v0.18.3
[0.18.2]: https://github.com/FoxIO-LLC/ja4/compare/v0.18.1...v0.18.2
[0.18.1]: https://github.com/FoxIO-LLC/ja4/compare/v0.18.0...v0.18.1
[0.18.0]: https://github.com/FoxIO-LLC/ja4/compare/v0.17.0...v0.18.0
[0.17.0]: https://github.com/FoxIO-LLC/ja4/compare/v0.16.2...v0.17.0
[0.16.2]: https://github.com/FoxIO-LLC/ja4/compare/v0.16.1...v0.16.2
[0.16.1]: https://github.com/FoxIO-LLC/ja4/compare/v0.16.0...v0.16.1
[0.16.0]: https://github.com/FoxIO-LLC/ja4/compare/v0.15.2...v0.16.0
[0.15.2]: https://github.com/FoxIO-LLC/ja4/compare/v0.15.1...v0.15.2
[0.15.1]: https://github.com/FoxIO-LLC/ja4/compare/v0.15.0...v0.15.1
[0.15.0]: https://github.com/FoxIO-LLC/ja4/compare/v0.14.0...v0.15.0
[0.14.0]: https://github.com/FoxIO-LLC/ja4/releases/tag/v0.14.0
