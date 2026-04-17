//! Text VDF parser powered by winnow.

use alloc::borrow::Cow;
use alloc::string::String;

use winnow::ascii::{line_ending, multispace1};
use winnow::combinator::{alt, delimited, preceded, repeat};
use winnow::error::{ContextError, StrContext};
use winnow::prelude::*;
use winnow::token::{one_of, take_till};

use crate::error::{Result, parse_error};
use crate::value::{Obj, Value, Vdf};

/// Parse a VDF document from text format.
///
/// # Example
///
/// ```
/// use steam_vdf_parser::parse_text;
///
/// let input = r#""root"
/// {
///     "key" "value"
/// }"#;
/// let vdf = parse_text(input).unwrap();
/// assert_eq!(vdf.key(), "root");
/// ```
pub fn parse(input: &str) -> Result<Vdf<'_>> {
    let mut input = input.trim_start();

    let key = token
        .parse_next(&mut input)
        .map_err(|_| parse_error(input, 0, "expected root key"))?;

    let obj = object
        .parse_next(&mut input)
        .map_err(|_| parse_error(input, 0, "expected root object"))?;

    Ok(Vdf::new(Cow::Borrowed(key), Value::Obj(obj)))
}

/// Parse a token (either quoted or unquoted).
fn token<'i>(input: &mut &'i str) -> ModalResult<&'i str> {
    preceded(whitespace, alt((quoted_string, unquoted_string))).parse_next(input)
}

/// Parse a quoted string, returning a Cow (borrowed if no escapes, owned if escapes processed).
///
/// Handles escape sequences: \n, \t, \r, \\, \"
fn quoted_string_cow<'i>(input: &mut &'i str) -> ModalResult<Cow<'i, str>> {
    // Parse opening quote
    '"'.parse_next(input)?;

    // Check if there are any escape sequences
    let content_end = input.find(['\\', '"']).unwrap_or(input.len());

    if content_end < input.len() && input[content_end..].starts_with('\\') {
        // Has escape sequences - need to process them
        let mut result = String::from(&input[..content_end]);
        *input = &input[content_end..];

        loop {
            // Check for closing quote
            if let Some(c) = input.chars().next() {
                if c == '"' {
                    *input = &input[c.len_utf8()..];
                    return Ok(Cow::Owned(result));
                }
                if c == '\\' {
                    // Escape sequence - consume backslash
                    *input = &input[c.len_utf8()..];

                    // Get escaped character
                    let escaped = one_of(('n', 't', 'r', '\\', '"'))
                        .map(|c| match c {
                            'n' => '\n',
                            't' => '\t',
                            'r' => '\r',
                            '\\' => '\\',
                            '"' => '"',
                            _ => unreachable!(),
                        })
                        .parse_next(input)?;
                    result.push(escaped);
                } else {
                    result.push(c);
                    *input = &input[c.len_utf8()..];
                }
            } else {
                // EOF before closing quote - fail
                return Err(winnow::error::ErrMode::Backtrack(ContextError::new()));
            }
        }
    } else {
        // No escapes - zero copy path
        let content = &input[..content_end];
        *input = &input[content_end..];

        // Parse closing quote
        '"'.parse_next(input)?;

        Ok(Cow::Borrowed(content))
    }
}

/// Parse a quoted string (borrowed version for key parsing).
/// Keys with escapes will fail - use quoted_string_cow for values that may have escapes.
fn quoted_string<'i>(input: &mut &'i str) -> ModalResult<&'i str> {
    '"'.parse_next(input)?;

    // Find closing quote, checking for escapes
    let mut end = 0;
    let mut chars = input.char_indices();
    while let Some((idx, c)) = chars.next() {
        if c == '"' {
            end = idx;
            break;
        }
        if c == '\\' {
            // Skip escaped character
            chars.next();
        }
    }

    if end == 0 {
        return Err(winnow::error::ErrMode::Backtrack(ContextError::new()));
    }

    let result = &input[..end];
    *input = &input[end + '"'.len_utf8()..];

    Ok(result)
}

/// Parse an unquoted string.
///
/// Unquoted strings end at whitespace, `{`, `}`, or `"`.
fn unquoted_string<'i>(input: &mut &'i str) -> ModalResult<&'i str> {
    take_till(1.., |c: char| {
        c.is_whitespace() || c == '{' || c == '}' || c == '"'
    })
    .context(StrContext::Label("token"))
    .parse_next(input)
}

/// Parse an object (recursive block of key-value pairs).
fn object<'i>(input: &mut &'i str) -> ModalResult<Obj<'i>> {
    preceded(
        whitespace,
        delimited('{', object_body, preceded(whitespace, '}')),
    )
    .context(StrContext::Label("object"))
    .parse_next(input)
}

/// Parse the body of an object (key-value pairs until closing brace).
fn object_body<'i>(input: &mut &'i str) -> ModalResult<Obj<'i>> {
    let mut obj = Obj::new();

    loop {
        // Skip whitespace
        whitespace.parse_next(input)?;

        // Check for closing brace
        if input.starts_with('}') {
            break;
        }

        // Parse a key-value pair
        let (key, value) = kv_pair.parse_next(input)?;
        obj.insert(Cow::Borrowed(key), value);
    }

    Ok(obj)
}

/// Parse a key-value pair.
fn kv_pair<'i>(input: &mut &'i str) -> ModalResult<(&'i str, Value<'i>)> {
    let key = token.parse_next(input)?;

    // Skip whitespace before value
    whitespace.parse_next(input)?;

    // Parse the value
    let value = if let Some(c) = input.chars().next() {
        match c {
            '{' => object.map(Value::Obj).parse_next(input)?,
            '"' => quoted_string_cow.map(Value::Str).parse_next(input)?,
            _ => unquoted_string
                .map(|s| Value::Str(Cow::Borrowed(s)))
                .parse_next(input)?,
        }
    } else {
        return Err(winnow::error::ErrMode::Backtrack(ContextError::new()));
    };

    Ok((key, value))
}

/// Skip whitespace and line comments.
fn whitespace(input: &mut &str) -> ModalResult<()> {
    repeat(0.., alt((multispace1.void(), line_comment.void()))).parse_next(input)
}

/// Parse a line comment (// to newline).
fn line_comment(input: &mut &str) -> ModalResult<()> {
    preceded(
        "//",
        alt((line_ending.void(), take_till(0.., ['\r', '\n']).void())),
    )
    .parse_next(input)
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::format;

    #[test]
    fn test_parse_simple_kv() {
        let input = r#""root"
        {
            "key" "value"
        }"#;
        let vdf = parse(input).unwrap();
        assert_eq!(vdf.key(), "root");

        let obj = vdf.as_obj().unwrap();
        let value = obj.get("key").and_then(|v| v.as_str());
        assert_eq!(value, Some("value"));
    }

    #[test]
    fn test_parse_nested_objects() {
        let input = r#""outer"
        {
            "inner"
            {
                "key" "value"
            }
        }"#;
        let vdf = parse(input).unwrap();
        assert_eq!(vdf.key(), "outer");

        let obj = vdf.as_obj().unwrap();
        let inner = obj.get("inner").and_then(|v| v.as_obj()).unwrap();
        let value = inner.get("key").and_then(|v| v.as_str());
        assert_eq!(value, Some("value"));
    }

    #[test]
    fn test_parse_unquoted_tokens() {
        let input = r#"root
        {
            key value
        }"#;
        let vdf = parse(input).unwrap();
        assert_eq!(vdf.key(), "root");

        let obj = vdf.as_obj().unwrap();
        let value = obj.get("key").and_then(|v| v.as_str());
        assert_eq!(value, Some("value"));
    }

    #[test]
    fn test_parse_with_comments() {
        let input = r#""root"
        {
            // This is a comment
            "key" "value"
            // Another comment
        }"#;
        let vdf = parse(input).unwrap();

        let obj = vdf.as_obj().unwrap();
        let value = obj.get("key").and_then(|v| v.as_str());
        assert_eq!(value, Some("value"));
    }

    #[test]
    fn test_parse_multiple_keys() {
        let input = r#""settings"
        {
            "name" "test"
            "count" "42"
        }"#;
        let vdf = parse(input).unwrap();

        let obj = vdf.as_obj().unwrap();
        assert_eq!(obj.get("name").and_then(|v| v.as_str()), Some("test"));
        assert_eq!(obj.get("count").and_then(|v| v.as_str()), Some("42"));
    }

    #[test]
    fn test_escape_sequences() {
        let test_cases: &[(&str, &str)] = &[
            (r#""test\nline""#, "test\nline"),
            (r#""test\ttab""#, "test\ttab"),
            (r#""test\\backslash""#, "test\\backslash"),
            (r#""test\"quote""#, "test\"quote"),
            (r#""test\rreturn""#, "test\rreturn"),
        ];

        for (input, expected) in test_cases {
            let full_input = format!(r#""root"{{"key" {}}}"#, input);
            let vdf = parse(&full_input).unwrap();
            let obj = vdf.as_obj().unwrap();
            let value = obj.get("key").and_then(|v| v.as_str()).unwrap();
            assert_eq!(value, *expected, "Failed for input: {}", input);
        }
    }

    #[test]
    fn test_escape_sequences_in_nested_objects() {
        let input = r#""root"
        {
            "outer"
            {
                "key" "value\nwith\nnewlines"
            }
        }"#;
        let vdf = parse(input).unwrap();
        let outer = vdf
            .as_obj()
            .unwrap()
            .get("outer")
            .and_then(|v| v.as_obj())
            .unwrap();
        let value = outer.get("key").and_then(|v| v.as_str()).unwrap();
        assert_eq!(value, "value\nwith\nnewlines");
    }

    #[test]
    fn test_mixed_escape_sequences() {
        let input = r#""root"{"key" "line1\nline2\ttab\\slash\"quote"}"#;
        let vdf = parse(input).unwrap();
        let obj = vdf.as_obj().unwrap();
        let value = obj.get("key").and_then(|v| v.as_str()).unwrap();
        assert_eq!(value, "line1\nline2\ttab\\slash\"quote");
    }

    #[test]
    fn test_unquoted_token_no_escape_processing() {
        let input = r#"root{key value\nnotescaped}"#;
        let vdf = parse(input).unwrap();
        let obj = vdf.as_obj().unwrap();
        let value = obj.get("key").and_then(|v| v.as_str()).unwrap();
        // Unquoted tokens should have literal backslash-n
        assert_eq!(value, r#"value\nnotescaped"#);
    }

    #[test]
    fn test_quoted_string_without_escapes_zero_copy() {
        let input = r#""root"{"key" "value"}"#;
        let vdf = parse(input).unwrap();
        let obj = vdf.as_obj().unwrap();
        let value = obj.get("key").and_then(|v| v.as_str()).unwrap();
        // Without escapes, value should be parsed correctly (zero-copy internally)
        assert_eq!(value, "value");
    }

    #[test]
    fn test_quoted_string_with_escapes_owned() {
        let input = r#""root"{"key" "value\nwith\nescape"}"#;
        let vdf = parse(input).unwrap();
        let obj = vdf.as_obj().unwrap();
        let value = obj.get("key").and_then(|v| v.as_str()).unwrap();
        // With escapes, value should be parsed correctly (owned internally)
        assert_eq!(value, "value\nwith\nescape");
    }

    #[test]
    fn test_empty_object() {
        let input = r#""root"{}"#;
        let vdf = parse(input).unwrap();
        let obj = vdf.as_obj().unwrap();
        assert!(obj.is_empty());
    }

    #[test]
    fn test_deeply_nested_objects() {
        let input = r#""root"
        {
            "level1"
            {
                "level2"
                {
                    "level3"
                    {
                        "key" "value"
                    }
                }
            }
        }"#;
        let vdf = parse(input).unwrap();
        let level1 = vdf
            .as_obj()
            .unwrap()
            .get("level1")
            .and_then(|v| v.as_obj())
            .unwrap();
        let level2 = level1.get("level2").and_then(|v| v.as_obj()).unwrap();
        let level3 = level2.get("level3").and_then(|v| v.as_obj()).unwrap();
        let value = level3.get("key").and_then(|v| v.as_str()).unwrap();
        assert_eq!(value, "value");
    }
}
