use std::ffi::{c_char, c_int, c_uint};
use std::slice;

#[repr(C)]
pub struct KeroseneUiTheme {
    pub tab_h: c_int,
    pub toolbar_h: c_int,
    pub bg: c_uint,
    pub surface: c_uint,
    pub surface_hot: c_uint,
    pub text: c_uint,
    pub muted: c_uint,
    pub accent: c_uint,
    pub page_bg: c_uint,
}

#[no_mangle]
pub extern "C" fn kerosene_ui_theme(out: *mut KeroseneUiTheme) {
    if out.is_null() {
        return;
    }
    unsafe {
        *out = KeroseneUiTheme {
            tab_h: 38,
            toolbar_h: 54,
            bg: 0xFFF1F3F4,
            surface: 0xFFFFFFFF,
            surface_hot: 0xFFE8EAED,
            text: 0xFF202124,
            muted: 0xFF5F6368,
            accent: 0xFF1A73E8,
            page_bg: 0xFFFFFFFF,
        };
    }
}

#[no_mangle]
pub extern "C" fn kerosene_ui_normalize_omnibox(
    input: *const c_char,
    out: *mut c_char,
    out_cap: usize,
) -> c_int {
    if input.is_null() || out.is_null() || out_cap == 0 {
        return 0;
    }

    let raw = unsafe {
        let mut len = 0usize;
        while *input.add(len) != 0 {
            len += 1;
        }
        let bytes = slice::from_raw_parts(input as *const u8, len);
        String::from_utf8_lossy(bytes).trim().to_string()
    };

    let normalized = if raw.is_empty() {
        "kerosene:home".to_string()
    } else if raw.eq_ignore_ascii_case("home") || raw.eq_ignore_ascii_case("kerosene:home") {
        "kerosene:home".to_string()
    } else if has_scheme(&raw) {
        raw
    } else if looks_like_host(&raw) {
        format!("https://{raw}")
    } else {
        format!("kerosene:search?q={}", percent_encode(&raw))
    };

    write_c_string(&normalized, out, out_cap)
}

fn has_scheme(value: &str) -> bool {
    let mut chars = value.chars();
    matches!(chars.next(), Some('a'..='z') | Some('A'..='Z'))
        && value
            .find(':')
            .map(|idx| value[..idx].chars().all(|c| c.is_ascii_alphanumeric() || matches!(c, '+' | '-' | '.')))
            .unwrap_or(false)
}

fn looks_like_host(value: &str) -> bool {
    !value.contains(' ')
        && (value.contains('.') || value.eq_ignore_ascii_case("localhost"))
        && !value.starts_with('.')
        && !value.ends_with('.')
}

fn percent_encode(value: &str) -> String {
    let mut out = String::with_capacity(value.len());
    for byte in value.bytes() {
        match byte {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'-' | b'_' | b'.' | b'~' => out.push(byte as char),
            b' ' => out.push('+'),
            _ => {
                const HEX: &[u8; 16] = b"0123456789ABCDEF";
                out.push('%');
                out.push(HEX[(byte >> 4) as usize] as char);
                out.push(HEX[(byte & 0x0F) as usize] as char);
            }
        }
    }
    out
}

fn write_c_string(value: &str, out: *mut c_char, out_cap: usize) -> c_int {
    let bytes = value.as_bytes();
    let count = bytes.len().min(out_cap.saturating_sub(1));
    unsafe {
        std::ptr::copy_nonoverlapping(bytes.as_ptr(), out as *mut u8, count);
        *out.add(count) = 0;
    }
    count as c_int
}
