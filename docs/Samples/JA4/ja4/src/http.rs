// Copyright (c) 2023, FoxIO, LLC.
// All rights reserved.
// Patent Pending
// JA4 is Open-Source, Licensed under BSD 3-Clause
// JA4+ (JA4S, JA4H, JA4L, JA4X, JA4SSH) are licenced under the FoxIO License 1.1.
// For full license text, see the repo root.

//! JA4H -- HTTP client fingerprinting

use std::fmt;

use itertools::Itertools as _;
use serde::Serialize;

use crate::{Error, FormatFlags, Packet, PacketNum, Proto, Result};

#[derive(Debug, Default)]
pub(crate) struct Stream(Vec<HttpStats>);

impl Stream {
    pub(crate) fn update(&mut self, pkt: &Packet, store_pkt_num: bool) -> Result<()> {
        let stats = if let Some(http) = pkt.find_proto("http") {
            HttpStats::from_http1(&http, store_pkt_num)?
        } else if let Some(http2) = pkt.find_proto("http2") {
            HttpStats::from_http2(&http2, store_pkt_num)?
        } else {
            None
        };
        self.0.extend(stats);
        Ok(())
    }

    pub(crate) fn into_out(self, flags: FormatFlags) -> Option<OutStream> {
        if self.0.is_empty() {
            None
        } else {
            let http = self.0.into_iter().map(|s| s.into_out(flags)).collect();
            Some(OutStream { http })
        }
    }
}

#[derive(Debug, Serialize)]
pub(crate) struct OutStream {
    http: Vec<OutHttp>,
}

#[derive(Debug)]
#[cfg_attr(test, derive(Clone))]
struct HttpStats {
    packet: Option<PacketNum>,
    req_method: HttpRequestMethod,
    version: HttpVersion,
    // "'Cause if you stay with us you're going to be pretty kooky too"
    has_cookie_header: bool,
    has_referer_header: bool,
    language: Option<String>,
    headers: Vec<String>,
    // Reference: https://datatracker.ietf.org/doc/html/rfc6265#section-4.2.1
    cookie_pairs: Vec<(String, Option<String>)>,
}

impl HttpStats {
    fn from_http1(http: &Proto, store_pkt_num: bool) -> Result<Option<Self>> {
        let req_method = match http.find("http.request.method") {
            Ok(md) => md.value().parse()?,
            Err(_) => return Ok(None),
        };
        let version = http.first("http.request.version")?.parse()?;
        let language = http.first("http.accept_language").ok().map(str::to_owned);

        let mut has_cookie_header = false;
        let mut has_referer_header = false;
        let headers = http
            .values("http.request.line")
            .filter_map(|s| {
                // SAFETY: `str::split` never returns an empty iterator, so it's safe to
                // unwrap.
                let s = s.split(':').next().unwrap().to_owned();
                // Field names are case-insensitive.
                // See https://www.rfc-editor.org/rfc/rfc2616#section-4.2
                match s.to_lowercase().as_str() {
                    "cookie" => {
                        has_cookie_header = true;
                        None
                    }
                    "referer" => {
                        has_referer_header = true;
                        None
                    }
                    _ => Some(s),
                }
            })
            .collect();

        let cookie_pairs = match http.first("http.cookie") {
            Err(_) => Vec::new(),
            Ok(s) => cookie_pairs(s.split("; ")).collect(),
        };

        Ok(Some(Self {
            packet: store_pkt_num.then_some(http.packet_num),
            req_method,
            version,
            has_cookie_header,
            has_referer_header,
            language,
            headers,
            cookie_pairs,
        }))
    }

    fn from_http2(http2: &Proto, store_pkt_num: bool) -> Result<Option<Self>> {
        let req_method = match http2.find("http2.headers.method") {
            Ok(md) => md.value().parse()?,
            Err(_) => return Ok(None),
        };
        let language = http2
            .first("http2.headers.accept_language")
            .ok()
            .map(str::to_owned);

        let mut has_cookie_header = false;
        let mut has_referer_header = false;
        let headers = http2
            .values("http2.header.name")
            .filter_map(|s| {
                // Just as in HTTP/1.x, header field names are strings of ASCII
                // characters that are compared in a case-insensitive fashion.  However,
                // header field names MUST be converted to lowercase prior to their
                // encoding in HTTP/2.  A request or response containing uppercase
                // header field names MUST be treated as malformed
                //
                // Reference: https://www.rfc-editor.org/rfc/rfc7540.html#section-8.1.2
                if s == "cookie" {
                    has_cookie_header = true;
                    None
                } else if s == "referer" {
                    has_referer_header = true;
                    None
                } else {
                    Some(s.to_owned())
                }
            })
            .collect();

        // Reference: https://datatracker.ietf.org/doc/html/rfc7540#section-8.1.2.5
        let cookie_pairs = cookie_pairs(http2.values("http2.headers.cookie")).collect();

        Ok(Some(Self {
            packet: store_pkt_num.then_some(http2.packet_num),
            req_method,
            version: HttpVersion::Http2,
            has_cookie_header,
            has_referer_header,
            language,
            headers,
            cookie_pairs,
        }))
    }

    fn into_out(self, flags: FormatFlags) -> OutHttp {
        let Self {
            packet,
            req_method,
            version,
            has_cookie_header,
            has_referer_header,
            language,
            headers,
            mut cookie_pairs,
        } = self;
        let FormatFlags {
            with_raw,
            original_order,
        } = flags;

        let cookie_marker = if has_cookie_header { 'c' } else { 'n' };
        let referer_marker = if has_referer_header { 'r' } else { 'n' };
        let nr_headers = 99.min(headers.len());
        let lang = truncate_to(4, primary_language(language.unwrap_or_default()));

        let first_chunk =
            format!("{req_method}{version}{cookie_marker}{referer_marker}{nr_headers:02}{lang}");

        if !original_order {
            cookie_pairs.sort_unstable();
        }
        let cookie_names = joined_cookie_names(&cookie_pairs);
        let cookies = joined_cookie_pairs(cookie_pairs);
        let headers = headers.into_iter().join(",");

        let ja4h_r = with_raw.then(|| {
            let s = format!("{first_chunk}_{headers}_{cookie_names}_{cookies}");
            if original_order {
                Ja4hRawFingerprint::Unsorted(s)
            } else {
                Ja4hRawFingerprint::Sorted(s)
            }
        });

        let headers = crate::hash12(headers);
        let cookie_names = crate::hash12(cookie_names);
        let cookies = crate::hash12(cookies);
        let ja4h = {
            let s = format!("{first_chunk}_{headers}_{cookie_names}_{cookies}");
            if original_order {
                Ja4hFingerprint::Unsorted(s)
            } else {
                Ja4hFingerprint::Sorted(s)
            }
        };

        OutHttp {
            pkt_ja4h: packet,
            ja4h,
            ja4h_r,
        }
    }
}

fn cookie_pairs<'a, I>(cookies: I) -> impl Iterator<Item = (String, Option<String>)> + 'a
where
    I: IntoIterator<Item = &'a str> + 'a,
{
    cookies
        .into_iter()
        .map(|cookie| match cookie.split_once('=') {
            None => (cookie.to_owned(), None),
            Some((name, value)) => (name.to_owned(), Some(value.to_owned())),
        })
}

fn joined_cookie_names<'a, I>(cookie_pairs: I) -> String
where
    I: IntoIterator<Item = &'a (String, Option<String>)>,
{
    cookie_pairs
        .into_iter()
        .map(|(name, _)| {
            assert!(!name.is_empty());
            name.to_owned()
        })
        .join(",")
}

fn joined_cookie_pairs<I>(cookie_pairs: I) -> String
where
    I: IntoIterator<Item = (String, Option<String>)>,
{
    cookie_pairs
        .into_iter()
        .map(|(name, value)| {
            assert!(!name.is_empty());
            match value {
                None => name.to_owned(),
                Some(value) => format!("{name}={value}"),
            }
        })
        .join(",")
}

#[derive(Debug, Serialize)]
pub(crate) struct OutHttp {
    #[serde(skip_serializing_if = "Option::is_none")]
    pkt_ja4h: Option<PacketNum>,
    #[serde(flatten)]
    ja4h: Ja4hFingerprint,
    #[serde(flatten, skip_serializing_if = "Option::is_none")]
    ja4h_r: Option<Ja4hRawFingerprint>,
}

#[derive(Debug, Serialize)]
enum Ja4hFingerprint {
    #[serde(rename = "ja4h")]
    Sorted(String),
    #[serde(rename = "ja4h_o")]
    Unsorted(String),
}

#[derive(Debug, Serialize)]
enum Ja4hRawFingerprint {
    #[serde(rename = "ja4h_r")]
    Sorted(String),
    #[serde(rename = "ja4h_ro")]
    Unsorted(String),
}

fn primary_language(lang: impl AsRef<str>) -> String {
    let lang = lang.as_ref();
    debug_assert!(!lang.to_lowercase().starts_with("accept-language: ")); // we've stripped the prefix already
    lang.trim_start()
        .split(',')
        .next()
        // SAFETY: `split` never returns an empty iterator, so it's safe to unwrap
        .unwrap()
        .replace('-', "")
        .to_lowercase()
}

#[test]
fn test_primary_language() {
    assert_eq!("da", primary_language("da, en-GB;q=0.8, en;q=0.7"));
    assert_eq!("enus", primary_language("en-US,en;q=0.9"));
}

/// Truncates the string to the given length, padding with zeros if necessary.
fn truncate_to(n: usize, mut s: String) -> String {
    let len = s.chars().count();

    #[allow(clippy::comparison_chain)]
    if len < n {
        s.push_str(&"0".repeat(n - len));
    } else if len > n {
        s.truncate(n);
    }
    s
}

#[test]
fn test_truncate_to() {
    assert_eq!(truncate_to(3, "abcd".to_owned()), "abc");
    assert_eq!(truncate_to(3, "abc".to_owned()), "abc");
    assert_eq!(truncate_to(3, "ab".to_owned()), "ab0");
    assert_eq!(truncate_to(3, "a".to_owned()), "a00");
    assert_eq!(truncate_to(3, "".to_owned()), "000");
}

#[derive(Debug, Clone, PartialEq)]
enum HttpRequestMethod {
    Connect,
    Delete,
    Get,
    Head,
    Options,
    Patch,
    Post,
    Put,
    Trace,
}

impl std::str::FromStr for HttpRequestMethod {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "CONNECT" => Ok(Self::Connect),
            "DELETE" => Ok(Self::Delete),
            "GET" => Ok(Self::Get),
            "HEAD" => Ok(Self::Head),
            "OPTIONS" => Ok(Self::Options),
            "PATCH" => Ok(Self::Patch),
            "POST" => Ok(Self::Post),
            "PUT" => Ok(Self::Put),
            "TRACE" => Ok(Self::Trace),
            _ => Err(Error::InvalidHttpRequest {
                field: "method".to_owned(),
                value: s.to_owned(),
            }),
        }
    }
}

impl fmt::Display for HttpRequestMethod {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let code = match self {
            Self::Connect => "co",
            Self::Delete => "de",
            Self::Get => "ge",
            Self::Head => "he",
            Self::Options => "op",
            Self::Patch => "pa",
            Self::Post => "po",
            Self::Put => "pu",
            Self::Trace => "tr",
        };
        f.write_str(code)
    }
}

#[test]
fn test_http_request_method() {
    assert_eq!(
        "CONNECT".parse::<HttpRequestMethod>().unwrap(),
        HttpRequestMethod::Connect
    );
    let Error::InvalidHttpRequest { field, value } =
        "connect".parse::<HttpRequestMethod>().unwrap_err()
    else {
        panic!();
    };
    assert_eq!(field, "method");
    assert_eq!(value, "connect");

    assert_eq!(HttpRequestMethod::Get.to_string(), "ge");
}

#[derive(Debug, Clone, PartialEq, PartialOrd)]
enum HttpVersion {
    Http1_0,
    Http1_1,
    Http2,
    Http3,
}

impl std::str::FromStr for HttpVersion {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "HTTP/1.0" => Ok(Self::Http1_0),
            "HTTP/1.1" => Ok(Self::Http1_1),
            "HTTP/2" => Ok(Self::Http2),
            "HTTP/3" => Ok(Self::Http3),
            _ => Err(Error::InvalidHttpRequest {
                field: "version".to_owned(),
                value: s.to_owned(),
            }),
        }
    }
}

impl fmt::Display for HttpVersion {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let code = match self {
            Self::Http1_0 => "10",
            Self::Http1_1 => "11",
            Self::Http2 => "20",
            Self::Http3 => "30",
        };
        f.write_str(code)
    }
}

#[test]
fn test_http_version() {
    assert_eq!(
        "HTTP/1.1".parse::<HttpVersion>().unwrap(),
        HttpVersion::Http1_1
    );
    let Error::InvalidHttpRequest { field, value } = "0.1.0".parse::<HttpVersion>().unwrap_err()
    else {
        panic!();
    };
    assert_eq!(field, "version");
    assert_eq!(value, "0.1.0");

    assert_eq!(HttpVersion::Http3.to_string(), "30");

    assert!(HttpVersion::Http1_0 < HttpVersion::Http1_1);
    assert!(HttpVersion::Http1_1 < HttpVersion::Http2);
    assert!(HttpVersion::Http2 < HttpVersion::Http3);
}

#[cfg(test)]
mod tests {
    use super::*;
    use expect_test::{expect, Expect};

    #[test]
    fn test_http_stats_into_out() {
        let pre_headers = [
            "Host: www.cnn.com\r\n",
            "Cookie: FastAB=0=6859,1=8174,2=4183,3=3319,4=3917,5=2557,6=4259,7=6070,8=0804,9=6453,10=1942,11=4435,12=4143,13=9445,14=6957,15=8682,16=1885,17=1825,18=3760,19=0929; sato=1; countryCode=US; stateCode=VA; geoData=purcellville|VA|20132|US|NA|-400|broadband|39.160|-77.700|511; usprivacy=1---; umto=1; _dd_s=logs=1&id=b5c2d770-eaba-4847-8202-390c4552ff9a&created=1686159462724&expire=1686160422726\r\n",
            "Sec-Ch-Ua: \r\n",
            "Sec-Ch-Ua-Mobile: ?0\r\n",
            "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/114.0.5735.110 Safari/537.36\r\n",
            "Sec-Ch-Ua-Platform: \"\"\r\n",
            "Accept: */*\r\n",
            "Sec-Fetch-Site: same-origin\r\n",
            "Sec-Fetch-Mode: cors\r\n",
            "Sec-Fetch-Dest: empty\r\n",
            "Sec-Fetch-Mode: cors\r\n",
            "Sec-Fetch-Dest: empty\r\n",
            "Referer: https://www.cnn.com/\r\n",
            "Accept-Encoding: gzip, deflate\r\n",
            "Accept-Language: en-US,en;q=0.9\r\n",
        ];
        let get_header_value = |header: &str| {
            pre_headers
                .iter()
                .find(|s| s.starts_with(header))
                .unwrap()
                .split(": ")
                .nth(1)
                .unwrap()
                .trim_end()
        };
        let language = Some(get_header_value("Accept-Language: ").to_owned());
        let cookie_pairs = cookie_pairs(get_header_value("Cookie: ").split("; ")).collect();
        let headers = pre_headers
            .into_iter()
            .map(|s| s.split(':').next().unwrap().to_owned())
            .filter(|s| s != "Cookie" && s != "Referer")
            .collect();

        let stats = HttpStats {
            packet: None,
            req_method: HttpRequestMethod::Get,
            version: HttpVersion::Http1_1,
            has_cookie_header: true,
            has_referer_header: true,
            language,
            headers,
            cookie_pairs,
        };

        let out = stats.clone().into_out(FormatFlags::default());
        expect![[r#"
            {
              "ja4h": "ge11cr13enus_88d2d584d47f_0f2659b474bf_161698816dab"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let out = stats.clone().into_out(FormatFlags {
            with_raw: true,
            ..Default::default()
        });
        expect![[r#"
            {
              "ja4h": "ge11cr13enus_88d2d584d47f_0f2659b474bf_161698816dab",
              "ja4h_r": "ge11cr13enus_Host,Sec-Ch-Ua,Sec-Ch-Ua-Mobile,User-Agent,Sec-Ch-Ua-Platform,Accept,Sec-Fetch-Site,Sec-Fetch-Mode,Sec-Fetch-Dest,Sec-Fetch-Mode,Sec-Fetch-Dest,Accept-Encoding,Accept-Language_FastAB,_dd_s,countryCode,geoData,sato,stateCode,umto,usprivacy_FastAB=0=6859,1=8174,2=4183,3=3319,4=3917,5=2557,6=4259,7=6070,8=0804,9=6453,10=1942,11=4435,12=4143,13=9445,14=6957,15=8682,16=1885,17=1825,18=3760,19=0929,_dd_s=logs=1&id=b5c2d770-eaba-4847-8202-390c4552ff9a&created=1686159462724&expire=1686160422726,countryCode=US,geoData=purcellville|VA|20132|US|NA|-400|broadband|39.160|-77.700|511,sato=1,stateCode=VA,umto=1,usprivacy=1---"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let out = stats.clone().into_out(FormatFlags {
            original_order: true,
            ..Default::default()
        });
        expect![[r#"
            {
              "ja4h_o": "ge11cr13enus_88d2d584d47f_457935509480_ff4b0b83634b"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let out = stats.clone().into_out(FormatFlags {
            with_raw: true,
            original_order: true,
        });
        expect![[r#"
            {
              "ja4h_o": "ge11cr13enus_88d2d584d47f_457935509480_ff4b0b83634b",
              "ja4h_ro": "ge11cr13enus_Host,Sec-Ch-Ua,Sec-Ch-Ua-Mobile,User-Agent,Sec-Ch-Ua-Platform,Accept,Sec-Fetch-Site,Sec-Fetch-Mode,Sec-Fetch-Dest,Sec-Fetch-Mode,Sec-Fetch-Dest,Accept-Encoding,Accept-Language_FastAB,sato,countryCode,stateCode,geoData,usprivacy,umto,_dd_s_FastAB=0=6859,1=8174,2=4183,3=3319,4=3917,5=2557,6=4259,7=6070,8=0804,9=6453,10=1942,11=4435,12=4143,13=9445,14=6957,15=8682,16=1885,17=1825,18=3760,19=0929,sato=1,countryCode=US,stateCode=VA,geoData=purcellville|VA|20132|US|NA|-400|broadband|39.160|-77.700|511,usprivacy=1---,umto=1,_dd_s=logs=1&id=b5c2d770-eaba-4847-8202-390c4552ff9a&created=1686159462724&expire=1686160422726"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());

        let stats = HttpStats {
            packet: Some(PacketNum(113)),
            ..stats
        };
        let out = stats.into_out(FormatFlags::default());
        expect![[r#"
            {
              "pkt_ja4h": 113,
              "ja4h": "ge11cr13enus_88d2d584d47f_0f2659b474bf_161698816dab"
            }"#]]
        .assert_eq(&serde_json::to_string_pretty(&out).unwrap());
    }

    #[test]
    fn test_cookie_pairs() {
        // No cookies
        assert!(cookie_pairs([]).next().is_none());

        assert_eq!(
            cookie_pairs(["no-value"]).collect::<Vec<_>>(),
            [("no-value".to_owned(), None)]
        );

        assert_eq!(
            cookie_pairs(["multiple=equal=signs"]).collect::<Vec<_>>(),
            [("multiple".to_owned(), Some("equal=signs".to_owned()))]
        );

        assert_eq!(
            cookie_pairs(["foo=bar", "baz=qux"]).collect::<Vec<_>>(),
            [
                ("foo".to_owned(), Some("bar".to_owned())),
                ("baz".to_owned(), Some("qux".to_owned())),
            ]
        );
    }

    #[test]
    fn test_cookie_pairs_sorting() {
        fn check(actual: impl Iterator<Item = (String, Option<String>)>, expect: Expect) {
            let actual: Vec<_> = actual.collect();
            expect.assert_debug_eq(&actual);
        }

        let mut pairs: Vec<_> = {
            let cookies = [
                "pardot=tee2foreb3fefpgvk8u1056vt3",
                "visitor_id413862-hash=1f00bdb076b5fb707c70254849819ec1797d3e27cef91a61a9488cb7ca0ebf77f226caa4075591b2591bf9a1ccdf29432c67379b",
                "visitor_id413862=286585660",
                "a=y=z",
                "a=x",
                "no-value",
            ];
            cookie_pairs(cookies).collect()
        };

        // Original order
        check(
            pairs.clone().into_iter(),
            expect![[r#"
                [
                    (
                        "pardot",
                        Some(
                            "tee2foreb3fefpgvk8u1056vt3",
                        ),
                    ),
                    (
                        "visitor_id413862-hash",
                        Some(
                            "1f00bdb076b5fb707c70254849819ec1797d3e27cef91a61a9488cb7ca0ebf77f226caa4075591b2591bf9a1ccdf29432c67379b",
                        ),
                    ),
                    (
                        "visitor_id413862",
                        Some(
                            "286585660",
                        ),
                    ),
                    (
                        "a",
                        Some(
                            "y=z",
                        ),
                    ),
                    (
                        "a",
                        Some(
                            "x",
                        ),
                    ),
                    (
                        "no-value",
                        None,
                    ),
                ]
            "#]],
        );

        // Sorted
        pairs.sort_unstable();
        check(
            pairs.into_iter(),
            expect![[r#"
                [
                    (
                        "a",
                        Some(
                            "x",
                        ),
                    ),
                    (
                        "a",
                        Some(
                            "y=z",
                        ),
                    ),
                    (
                        "no-value",
                        None,
                    ),
                    (
                        "pardot",
                        Some(
                            "tee2foreb3fefpgvk8u1056vt3",
                        ),
                    ),
                    (
                        "visitor_id413862",
                        Some(
                            "286585660",
                        ),
                    ),
                    (
                        "visitor_id413862-hash",
                        Some(
                            "1f00bdb076b5fb707c70254849819ec1797d3e27cef91a61a9488cb7ca0ebf77f226caa4075591b2591bf9a1ccdf29432c67379b",
                        ),
                    ),
                ]
            "#]],
        );
    }

    #[test]
    fn test_joined_cookies() {
        let cookie_pairs = [
            ("a".to_owned(), Some("1".to_owned())),
            ("b".to_owned(), None),
            ("c".to_owned(), Some("3".to_owned())),
            ("d".to_owned(), Some(String::new())),
        ];

        assert_eq!(joined_cookie_names(&cookie_pairs), "a,b,c,d");
        assert_eq!(joined_cookie_pairs(cookie_pairs), "a=1,b,c=3,d=");
    }
}
