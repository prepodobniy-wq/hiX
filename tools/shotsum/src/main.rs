// SPDX-License-Identifier: GPL-2.0-only
//! shotsum: golden checksums for the desktop frames.
//!
//! `make shots-check` renders every frame of the desktop tests as a binary
//! PPM (the pixels steel draws, byte for byte) and hands the directory to this
//! tool:
//!
//!     shotsum <dir>                    print "name WxH hash" for each frame
//!     shotsum <dir> --write <file>     store them as the golden manifest
//!     shotsum <dir> --check <file>     compare; exit 1 on any difference
//!
//! The hash is 64-bit FNV-1a over the pixel bytes. It is not cryptographic;
//! it only has to notice that a pixel moved. No dependencies, so it builds
//! offline anywhere there is a Rust toolchain.

use std::collections::BTreeMap;
use std::env;
use std::fs;
use std::path::Path;
use std::process::ExitCode;

#[derive(Debug, PartialEq, Eq, Clone)]
struct Frame {
    width: usize,
    height: usize,
    hash: u64,
}

/// Parse a binary PPM (P6, maxval 255) and hash its pixels.
fn parse_ppm(data: &[u8]) -> Result<Frame, String> {
    let mut pos = 0;
    let mut fields = Vec::new();

    while fields.len() < 4 {
        while pos < data.len() && data[pos].is_ascii_whitespace() {
            pos += 1;
        }
        if pos < data.len() && data[pos] == b'#' {
            while pos < data.len() && data[pos] != b'\n' {
                pos += 1;
            }
            continue;
        }
        let start = pos;
        while pos < data.len() && !data[pos].is_ascii_whitespace() {
            pos += 1;
        }
        if start == pos {
            return Err("truncated header".into());
        }
        fields.push(String::from_utf8_lossy(&data[start..pos]).into_owned());
    }
    pos += 1; // the single whitespace byte after maxval

    if fields[0] != "P6" {
        return Err(format!("not a P6 file ({})", fields[0]));
    }
    let width: usize = fields[1].parse().map_err(|_| "bad width")?;
    let height: usize = fields[2].parse().map_err(|_| "bad height")?;
    if fields[3] != "255" {
        return Err(format!("unsupported maxval {}", fields[3]));
    }
    let need = width * height * 3;
    let pixels = data.get(pos..pos + need).ok_or("pixel data is short")?;
    Ok(Frame { width, height, hash: fnv1a(pixels) })
}

fn fnv1a(bytes: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for &b in bytes {
        h ^= u64::from(b);
        h = h.wrapping_mul(0x0000_0100_0000_01b3);
    }
    h
}

fn read_frames(dir: &Path) -> Result<BTreeMap<String, Frame>, String> {
    let mut out = BTreeMap::new();
    let entries = fs::read_dir(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
    for entry in entries {
        let path = entry.map_err(|e| e.to_string())?.path();
        if path.extension().and_then(|e| e.to_str()) != Some("ppm") {
            continue;
        }
        let name = path.file_stem().unwrap().to_string_lossy().into_owned();
        let data = fs::read(&path).map_err(|e| format!("{}: {e}", path.display()))?;
        let frame = parse_ppm(&data).map_err(|e| format!("{}: {e}", path.display()))?;
        out.insert(name, frame);
    }
    Ok(out)
}

fn render(frames: &BTreeMap<String, Frame>) -> String {
    let mut s = String::new();
    for (name, f) in frames {
        s.push_str(&format!("{name} {}x{} {:016x}\n", f.width, f.height, f.hash));
    }
    s
}

fn parse_manifest(text: &str) -> Result<BTreeMap<String, Frame>, String> {
    let mut out = BTreeMap::new();
    for (i, line) in text.lines().enumerate() {
        let line = line.trim();
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let parts: Vec<&str> = line.split_whitespace().collect();
        let bad = || format!("manifest line {}: expected 'name WxH hash'", i + 1);
        if parts.len() != 3 {
            return Err(bad());
        }
        let (w, h) = parts[1].split_once('x').ok_or_else(bad)?;
        out.insert(
            parts[0].to_string(),
            Frame {
                width: w.parse().map_err(|_| bad())?,
                height: h.parse().map_err(|_| bad())?,
                hash: u64::from_str_radix(parts[2], 16).map_err(|_| bad())?,
            },
        );
    }
    Ok(out)
}

/// Differences between the golden manifest and the frames just rendered.
fn compare(golden: &BTreeMap<String, Frame>, now: &BTreeMap<String, Frame>) -> Vec<String> {
    let mut problems = Vec::new();
    for (name, g) in golden {
        match now.get(name) {
            None => problems.push(format!("missing   {name}")),
            Some(f) if f != g => problems.push(format!(
                "changed   {name}: {:016x} -> {:016x}",
                g.hash, f.hash
            )),
            Some(_) => {}
        }
    }
    for name in now.keys().filter(|n| !golden.contains_key(*n)) {
        problems.push(format!("new       {name}"));
    }
    problems
}

fn run(args: &[String]) -> Result<bool, String> {
    let dir = args.first().ok_or("usage: shotsum <dir> [--write F | --check F]")?;
    let frames = read_frames(Path::new(dir))?;
    if frames.is_empty() {
        return Err(format!("{dir}: no .ppm frames"));
    }
    match (args.get(1).map(String::as_str), args.get(2)) {
        (None, _) => {
            print!("{}", render(&frames));
            Ok(true)
        }
        (Some("--write"), Some(file)) => {
            fs::write(file, render(&frames)).map_err(|e| format!("{file}: {e}"))?;
            println!("wrote {} frames to {file}", frames.len());
            Ok(true)
        }
        (Some("--check"), Some(file)) => {
            let text = fs::read_to_string(file).map_err(|e| format!("{file}: {e}"))?;
            let problems = compare(&parse_manifest(&text)?, &frames);
            if problems.is_empty() {
                println!("shotsum: {} frames match {file}", frames.len());
                Ok(true)
            } else {
                for p in &problems {
                    eprintln!("{p}");
                }
                eprintln!("shotsum: {} difference(s); rerun with --write if intended", problems.len());
                Ok(false)
            }
        }
        _ => Err("usage: shotsum <dir> [--write F | --check F]".into()),
    }
}

fn main() -> ExitCode {
    let args: Vec<String> = env::args().skip(1).collect();
    match run(&args) {
        Ok(true) => ExitCode::SUCCESS,
        Ok(false) => ExitCode::from(1),
        Err(e) => {
            eprintln!("shotsum: {e}");
            ExitCode::from(2)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn ppm(w: usize, h: usize, fill: u8) -> Vec<u8> {
        let mut v = format!("P6\n{w} {h}\n255\n").into_bytes();
        v.extend(std::iter::repeat(fill).take(w * h * 3));
        v
    }

    #[test]
    fn hashes_known_values() {
        assert_eq!(fnv1a(b""), 0xcbf2_9ce4_8422_2325);
        assert_eq!(fnv1a(b"a"), 0xaf63_dc4c_8601_ec8c);
    }

    #[test]
    fn parses_header_and_comments() {
        let mut d = b"P6\n# made by steel\n2 1\n255\n".to_vec();
        d.extend([1, 2, 3, 4, 5, 6]);
        let f = parse_ppm(&d).unwrap();
        assert_eq!((f.width, f.height), (2, 1));
        assert_eq!(f.hash, fnv1a(&[1, 2, 3, 4, 5, 6]));
    }

    #[test]
    fn rejects_bad_files() {
        assert!(parse_ppm(b"P5\n1 1\n255\n\0").is_err());
        assert!(parse_ppm(b"P6\n2 2\n255\nxx").is_err());
        assert!(parse_ppm(b"P6\n1 1\n65535\n\0\0\0\0\0\0").is_err());
        assert!(parse_ppm(b"").is_err());
    }

    #[test]
    fn manifest_round_trip_and_compare() {
        let mut a = BTreeMap::new();
        a.insert("00-desk".to_string(), parse_ppm(&ppm(4, 4, 9)).unwrap());
        a.insert("01-term".to_string(), parse_ppm(&ppm(4, 4, 7)).unwrap());
        let back = parse_manifest(&render(&a)).unwrap();
        assert_eq!(back, a);
        assert!(compare(&back, &a).is_empty());

        let mut b = a.clone();
        b.insert("01-term".to_string(), parse_ppm(&ppm(4, 4, 8)).unwrap());
        b.insert("02-new".to_string(), parse_ppm(&ppm(4, 4, 1)).unwrap());
        b.remove("00-desk");
        let p = compare(&a, &b);
        assert_eq!(p.len(), 3);
        assert!(p.iter().any(|s| s.starts_with("missing   00-desk")));
        assert!(p.iter().any(|s| s.starts_with("changed   01-term")));
        assert!(p.iter().any(|s| s.starts_with("new       02-new")));
    }

    #[test]
    fn manifest_errors_name_the_line() {
        let e = parse_manifest("ok 1x1 00\nbroken\n").unwrap_err();
        assert!(e.contains("line 2"));
    }
}
