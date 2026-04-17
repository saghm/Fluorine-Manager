//! Unit tests for VDF types.

use super::types::{Obj, Value, Vdf};
use alloc::borrow::Cow;
use alloc::format;
use alloc::string::{String, ToString};

// ============================================================================
// From implementation tests
// ============================================================================

#[test]
fn test_value_from_str() {
    let value: Value = "hello".into();
    assert!(value.is_str());
    assert_eq!(value.as_str(), Some("hello"));
}

#[test]
fn test_value_from_string() {
    let value: Value = String::from("hello").into();
    assert!(value.is_str());
    assert_eq!(value.as_str(), Some("hello"));
}

#[test]
fn test_value_from_i32() {
    let value: Value = 42i32.into();
    assert!(value.is_i32());
    assert_eq!(value.as_i32(), Some(42));
}

#[test]
fn test_value_from_u64() {
    let value: Value = 123u64.into();
    assert!(value.is_u64());
    assert_eq!(value.as_u64(), Some(123));
}

#[test]
fn test_value_from_f32() {
    let value: Value = 2.5f32.into();
    assert!(value.is_float());
    assert_eq!(value.as_float(), Some(2.5));
}

#[test]
fn test_value_from_u32() {
    let value: Value = 0x12345678u32.into();
    assert!(value.is_pointer());
    assert_eq!(value.as_pointer(), Some(0x12345678));
}

#[test]
fn test_value_from_color() {
    let value: Value = [255u8, 0, 128, 64].into();
    assert!(value.is_color());
    assert_eq!(value.as_color(), Some([255, 0, 128, 64]));
}

#[test]
fn test_value_from_obj() {
    let obj = Obj::new();
    let value: Value = obj.into();
    assert!(value.is_obj());
}

// ============================================================================
// Type check tests
// ============================================================================

#[test]
fn test_value_is_methods() {
    let v = Value::I32(42);
    assert!(v.is_i32());
    assert!(!v.is_str());
    assert!(!v.is_obj());
}

#[test]
fn test_value_is_str() {
    let v = Value::Str(Cow::Borrowed("test"));
    assert!(v.is_str());
    assert!(!v.is_i32());
}

#[test]
fn test_value_is_obj() {
    let v = Value::Obj(Obj::new());
    assert!(v.is_obj());
    assert!(!v.is_str());
}

#[test]
fn test_value_is_float() {
    let v = Value::Float(1.0);
    assert!(v.is_float());
    assert!(!v.is_i32());
}

#[test]
fn test_value_is_u64() {
    let v = Value::U64(100);
    assert!(v.is_u64());
    assert!(!v.is_i32());
}

#[test]
fn test_value_is_pointer() {
    let v = Value::Pointer(0x12345678);
    assert!(v.is_pointer());
    assert!(!v.is_i32());
}

#[test]
fn test_value_is_color() {
    let v = Value::Color([255, 0, 0, 255]);
    assert!(v.is_color());
    assert!(!v.is_i32());
}

#[test]
fn test_value_as_methods() {
    let v = Value::I32(42);
    assert_eq!(v.as_i32(), Some(42));
    assert_eq!(v.as_str(), None);
    assert_eq!(v.as_obj(), None);
}

#[test]
fn test_value_as_str() {
    let v = Value::Str(Cow::Borrowed("test"));
    assert_eq!(v.as_str(), Some("test"));
}

#[test]
fn test_value_as_obj() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let v = Value::Obj(obj);
    assert!(v.as_obj().is_some());
}

#[test]
fn test_value_as_float() {
    let v = Value::Float(1.5);
    assert_eq!(v.as_float(), Some(1.5));
}

#[test]
fn test_value_as_u64() {
    let v = Value::U64(123456789);
    assert_eq!(v.as_u64(), Some(123456789));
}

#[test]
fn test_value_as_pointer() {
    let v = Value::Pointer(0xABCDEF01);
    assert_eq!(v.as_pointer(), Some(0xABCDEF01));
}

#[test]
fn test_value_as_color() {
    let v = Value::Color([255, 128, 64, 32]);
    assert_eq!(v.as_color(), Some([255, 128, 64, 32]));
}

#[test]
fn test_value_display_i32() {
    assert_eq!(format!("{}", Value::I32(42)), "\"42\"");
    assert_eq!(format!("{}", Value::I32(-42)), "\"-42\"");
}

#[test]
fn test_value_display_u64() {
    assert_eq!(format!("{}", Value::U64(100)), "\"100\"");
}

#[test]
fn test_value_display_str() {
    assert_eq!(format!("{}", Value::Str(Cow::Borrowed("test"))), "\"test\"");
}

#[test]
fn test_value_display_obj() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let v = Value::Obj(obj);
    assert!(format!("{}", v).contains("\"key\""));
    assert!(format!("{}", v).contains("\"42\""));
}

#[test]
fn test_value_display_float() {
    let v = Value::Float(1.5);
    assert_eq!(format!("{}", v), "\"1.5\"");
}

#[test]
fn test_value_display_pointer() {
    assert_eq!(format!("{}", Value::Pointer(0x12345678)), "\"0x12345678\"");
}

#[test]
fn test_value_display_color() {
    assert_eq!(
        format!("{}", Value::Color([255, 0, 0, 255])),
        "\"255 0 0 255\""
    );
}

#[test]
fn test_obj_new_is_empty() {
    let obj = Obj::new();
    assert!(obj.is_empty());
    assert_eq!(obj.len(), 0);
}

#[test]
fn test_obj_get() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    assert_eq!(obj.get("key").and_then(|v| v.as_i32()), Some(42));
    assert_eq!(obj.get("missing"), None);
}

#[test]
fn test_obj_len() {
    let mut obj = Obj::new();
    assert_eq!(obj.len(), 0);
    obj.insert(Cow::Borrowed("key1"), Value::I32(1));
    assert_eq!(obj.len(), 1);
    obj.insert(Cow::Borrowed("key2"), Value::I32(2));
    assert_eq!(obj.len(), 2);
}

#[test]
fn test_obj_iter() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key1"), Value::I32(1));
    obj.insert(Cow::Borrowed("key2"), Value::I32(2));
    let mut iter = obj.iter();
    assert!(iter.next().is_some());
    assert!(iter.next().is_some());
    assert!(iter.next().is_none());
}

#[test]
fn test_obj_default() {
    let obj = Obj::default();
    assert!(obj.is_empty());
}

#[test]
fn test_vdf_new() {
    let vdf = Vdf::new("root", Value::Obj(Obj::new()));
    assert_eq!(vdf.key(), "root");
    assert!(vdf.is_obj());
}

#[test]
fn test_vdf_is_obj() {
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(Obj::new()));
    assert!(vdf.is_obj());
    let vdf2 = Vdf::new(Cow::Borrowed("root"), Value::I32(42));
    assert!(!vdf2.is_obj());
}

#[test]
fn test_vdf_as_obj() {
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(Obj::new()));
    assert!(vdf.as_obj().is_some());
    let vdf2 = Vdf::new(Cow::Borrowed("root"), Value::I32(42));
    assert!(vdf2.as_obj().is_none());
}

#[test]
fn test_vdf_display() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(obj));
    let s = format!("{}", vdf);
    assert!(s.contains("root"));
}

#[test]
fn test_into_owned_value_str() {
    let value = Value::Str(Cow::Borrowed("test"));
    let owned = value.into_owned();
    assert_eq!(owned, Value::Str(Cow::Owned("test".to_string())));
}

#[test]
fn test_into_owned_value_str_already_owned() {
    let value = Value::Str(Cow::Owned("test".to_string()));
    let owned = value.into_owned();
    assert_eq!(owned, Value::Str(Cow::Owned("test".to_string())));
}

#[test]
fn test_into_owned_value_obj() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let value = Value::Obj(obj);
    let owned = value.into_owned();
    assert!(owned.is_obj());
}

#[test]
fn test_into_owned_value_numeric() {
    assert_eq!(Value::I32(42).into_owned(), Value::I32(42));
    assert_eq!(Value::U64(100).into_owned(), Value::U64(100));
    assert_eq!(Value::Float(1.5).into_owned(), Value::Float(1.5));
    assert_eq!(Value::Pointer(123).into_owned(), Value::Pointer(123));
    assert_eq!(
        Value::Color([1, 2, 3, 4]).into_owned(),
        Value::Color([1, 2, 3, 4])
    );
}

#[test]
fn test_into_owned_obj() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let owned = obj.into_owned();
    assert_eq!(owned.len(), 1);
    assert_eq!(owned.get("key").and_then(|v| v.as_i32()), Some(42));
}

#[test]
fn test_into_owned_obj_nested() {
    let mut inner = Obj::new();
    inner.insert(
        Cow::Borrowed("inner_key"),
        Value::Str(Cow::Borrowed("value")),
    );
    let mut outer = Obj::new();
    outer.insert(Cow::Borrowed("outer_key"), Value::Obj(inner));
    let owned = outer.into_owned();
    let inner_obj = owned.get("outer_key").and_then(|v| v.as_obj()).unwrap();
    assert_eq!(
        inner_obj.get("inner_key").and_then(|v| v.as_str()),
        Some("value")
    );
}

#[test]
fn test_into_owned_vdf() {
    let vdf = Vdf::new("root", Value::I32(42));
    let owned = vdf.into_owned();
    assert_eq!(owned.key(), "root");
    assert_eq!(owned.value(), &Value::I32(42));
}

// Tests for Value::get and get_path methods

#[test]
fn test_value_get_on_obj() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let value = Value::Obj(obj);
    assert_eq!(value.get("key").and_then(|v| v.as_i32()), Some(42));
    assert_eq!(value.get("missing"), None);
}

#[test]
fn test_value_get_on_non_obj() {
    let value = Value::I32(42);
    assert_eq!(value.get("key"), None);
}

#[test]
fn test_value_get_path_single_key() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let value = Value::Obj(obj);
    assert_eq!(value.get_path(&["key"]).and_then(|v| v.as_i32()), Some(42));
}

#[test]
fn test_value_get_path_empty() {
    let value = Value::I32(42);
    assert_eq!(value.get_path(&[]).and_then(|v| v.as_i32()), Some(42));
}

#[test]
fn test_value_get_path_nested() {
    let mut inner = Obj::new();
    inner.insert(Cow::Borrowed("c"), Value::Str(Cow::Borrowed("found")));
    let mut middle = Obj::new();
    middle.insert(Cow::Borrowed("b"), Value::Obj(inner));
    let mut outer = Obj::new();
    outer.insert(Cow::Borrowed("a"), Value::Obj(middle));
    let value = Value::Obj(outer);

    assert_eq!(
        value.get_path(&["a", "b", "c"]).and_then(|v| v.as_str()),
        Some("found")
    );
}

#[test]
fn test_value_get_path_missing_intermediate() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("a"), Value::I32(42));
    let value = Value::Obj(obj);
    assert_eq!(value.get_path(&["a", "b"]), None);
}

#[test]
fn test_value_get_path_non_obj_intermediate() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("a"), Value::I32(42));
    let value = Value::Obj(obj);
    assert_eq!(value.get_path(&["a", "b"]), None);
}

#[test]
fn test_value_get_str() {
    let mut inner = Obj::new();
    inner.insert(Cow::Borrowed("name"), Value::Str(Cow::Borrowed("test")));
    let mut outer = Obj::new();
    outer.insert(Cow::Borrowed("data"), Value::Obj(inner));
    let value = Value::Obj(outer);

    assert_eq!(value.get_str(&["data", "name"]), Some("test"));
    assert_eq!(value.get_str(&["data", "missing"]), None);
}

#[test]
fn test_value_get_obj() {
    let mut inner = Obj::new();
    inner.insert(Cow::Borrowed("key"), Value::I32(1));
    let mut outer = Obj::new();
    outer.insert(Cow::Borrowed("nested"), Value::Obj(inner));
    let value = Value::Obj(outer);

    let obj = value.get_obj(&["nested"]).unwrap();
    assert_eq!(obj.len(), 1);
}

#[test]
fn test_value_get_i32() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("num"), Value::I32(123));
    let value = Value::Obj(obj);

    assert_eq!(value.get_i32(&["num"]), Some(123));
    assert_eq!(value.get_i32(&["missing"]), None);
}

#[test]
fn test_value_get_u64() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("big"), Value::U64(9999999999));
    let value = Value::Obj(obj);

    assert_eq!(value.get_u64(&["big"]), Some(9999999999));
}

#[test]
fn test_value_get_float() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("ratio"), Value::Float(2.5));
    let value = Value::Obj(obj);

    assert_eq!(value.get_float(&["ratio"]), Some(2.5));
}

// Tests for Vdf delegation methods

#[test]
fn test_vdf_get() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("key"), Value::I32(42));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(obj));

    assert_eq!(vdf.get("key").and_then(|v| v.as_i32()), Some(42));
    assert_eq!(vdf.get("missing"), None);
}

#[test]
fn test_vdf_get_path() {
    let mut inner = Obj::new();
    inner.insert(Cow::Borrowed("value"), Value::Str(Cow::Borrowed("found")));
    let mut outer = Obj::new();
    outer.insert(Cow::Borrowed("nested"), Value::Obj(inner));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(outer));

    assert_eq!(
        vdf.get_path(&["nested", "value"]).and_then(|v| v.as_str()),
        Some("found")
    );
}

#[test]
fn test_vdf_get_str() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("name"), Value::Str(Cow::Borrowed("test")));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(obj));

    assert_eq!(vdf.get_str(&["name"]), Some("test"));
}

#[test]
fn test_vdf_get_obj() {
    let mut inner = Obj::new();
    inner.insert(Cow::Borrowed("k"), Value::I32(1));
    let mut outer = Obj::new();
    outer.insert(Cow::Borrowed("inner"), Value::Obj(inner));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(outer));

    assert!(vdf.get_obj(&["inner"]).is_some());
}

#[test]
fn test_vdf_get_i32() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("num"), Value::I32(42));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(obj));

    assert_eq!(vdf.get_i32(&["num"]), Some(42));
}

#[test]
fn test_vdf_get_u64() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("big"), Value::U64(12345678901234));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(obj));

    assert_eq!(vdf.get_u64(&["big"]), Some(12345678901234));
}

#[test]
fn test_vdf_get_float() {
    let mut obj = Obj::new();
    obj.insert(Cow::Borrowed("f"), Value::Float(2.5));
    let vdf = Vdf::new(Cow::Borrowed("root"), Value::Obj(obj));

    assert_eq!(vdf.get_float(&["f"]), Some(2.5));
}

// ============================================================================
// IntoIterator tests
// ============================================================================

#[test]
fn test_obj_into_iter_owned() {
    let mut obj = Obj::new();
    obj.insert("key1", "value1".into());
    obj.insert("key2", "value2".into());

    let mut count = 0;
    for (key, value) in obj {
        count += 1;
        assert!(key == "key1" || key == "key2");
        assert!(value.is_str());
    }
    assert_eq!(count, 2);
}

#[test]
fn test_obj_into_iter_ref() {
    let mut obj = Obj::new();
    obj.insert("key1", "value1".into());
    obj.insert("key2", "value2".into());

    let mut count = 0;
    for (key, value) in &obj {
        count += 1;
        assert!(key.as_ref() == "key1" || key.as_ref() == "key2");
        assert!(value.is_str());
    }
    assert_eq!(count, 2);
    // obj is still usable after iterating by reference
    assert_eq!(obj.len(), 2);
}

// ============================================================================
// Index trait tests
// ============================================================================

#[test]
fn test_value_index_existing_key() {
    let mut obj = Obj::new();
    obj.insert("name", "Alice".into());
    let value = Value::Obj(obj);

    assert_eq!(value["name"].as_str(), Some("Alice"));
}

#[test]
#[should_panic(expected = "key not found in Value")]
fn test_value_index_missing_key() {
    let mut obj = Obj::new();
    obj.insert("name", "Alice".into());
    let value = Value::Obj(obj);

    // This should panic because the key doesn't exist
    let _ = &value["nonexistent"];
}

#[test]
#[should_panic(expected = "key not found in Value")]
fn test_value_index_on_non_obj() {
    let value = Value::Str("not an object".into());

    // This should panic because indexing a non-object fails
    let _ = &value["any"];
}

#[test]
fn test_obj_index_existing_key() {
    let mut obj = Obj::new();
    obj.insert("count", 42i32.into());

    assert_eq!(obj["count"].as_i32(), Some(42));
}

#[test]
#[should_panic(expected = "key not found in Obj")]
fn test_obj_index_missing_key() {
    let obj = Obj::new();

    // This should panic because the key doesn't exist
    let _ = &obj["nonexistent"];
}

#[test]
fn test_index_chained_access() {
    let mut inner = Obj::new();
    inner.insert("value", "found".into());

    let mut outer = Obj::new();
    outer.insert("inner", Value::Obj(inner));

    let value = Value::Obj(outer);

    // Chained indexing works when keys exist
    assert_eq!(value["inner"]["value"].as_str(), Some("found"));
}

#[test]
#[should_panic(expected = "key not found in Value")]
fn test_index_chained_access_missing_key() {
    let mut inner = Obj::new();
    inner.insert("value", "found".into());

    let mut outer = Obj::new();
    outer.insert("inner", Value::Obj(inner));

    let value = Value::Obj(outer);

    // This should panic because "missing" doesn't exist
    let _ = &value["missing"]["anything"];
}

// ============================================================================
// Pretty-print Display tests
// ============================================================================

#[test]
fn test_value_pretty_str() {
    let value = Value::Str(Cow::Borrowed("hello"));
    assert_eq!(format!("{:#}", value), "\"hello\"");
}

#[test]
fn test_value_pretty_str_with_escapes() {
    let value = Value::Str(Cow::Borrowed("line1\nline2\ttab\\backslash\"quote"));
    assert_eq!(
        format!("{:#}", value),
        "\"line1\\nline2\\ttab\\\\backslash\\\"quote\""
    );
}

#[test]
fn test_value_pretty_i32() {
    let value = Value::I32(42);
    assert_eq!(format!("{:#}", value), "\"42\"");
}

#[test]
fn test_value_pretty_u64() {
    let value = Value::U64(123456789);
    assert_eq!(format!("{:#}", value), "\"123456789\"");
}

#[test]
fn test_value_pretty_float() {
    let value = Value::Float(1.5);
    assert_eq!(format!("{:#}", value), "\"1.5\"");
}

#[test]
fn test_value_pretty_pointer() {
    let value = Value::Pointer(0x12345678);
    assert_eq!(format!("{:#}", value), "\"0x12345678\"");
}

#[test]
fn test_value_pretty_color() {
    let value = Value::Color([255, 0, 128, 64]);
    assert_eq!(format!("{:#}", value), "\"255 0 128 64\"");
}

#[test]
fn test_obj_pretty_simple() {
    let mut obj = Obj::new();
    obj.insert("key", "value".into());
    let output = format!("{:#}", obj);
    // Should contain proper VDF structure
    assert!(output.starts_with("{\n"));
    assert!(output.ends_with("}"));
    assert!(output.contains("\"key\""));
    assert!(output.contains("\"value\""));
}

#[test]
fn test_obj_pretty_nested() {
    let mut inner = Obj::new();
    inner.insert("inner_key", "inner_value".into());
    let mut outer = Obj::new();
    outer.insert("nested", Value::Obj(inner));

    let output = format!("{:#}", outer);
    // Should have nested structure with proper indentation
    assert!(output.contains("\"nested\""));
    assert!(output.contains("\"inner_key\""));
    assert!(output.contains("\"inner_value\""));
    // Check for proper indentation (tabs)
    assert!(output.contains("\t\"nested\""));
    assert!(output.contains("\t\t\"inner_key\""));
}

#[test]
fn test_vdf_pretty() {
    let mut obj = Obj::new();
    obj.insert("key", "value".into());
    let vdf = Vdf::new("root", Value::Obj(obj));

    let output = format!("{:#}", vdf);
    // Should start with quoted key followed by newline and brace
    assert!(output.starts_with("\"root\"\n{"));
    assert!(output.contains("\"key\"\t\"value\""));
}

#[test]
fn test_vdf_pretty_complex() {
    let mut inner = Obj::new();
    inner.insert("value", "found".into());
    inner.insert("number", Value::I32(42));

    let mut outer = Obj::new();
    outer.insert("nested", Value::Obj(inner));
    outer.insert("simple", "string".into());

    let vdf = Vdf::new("TestVdf", Value::Obj(outer));

    let output = format!("{:#}", vdf);
    // Verify structure
    assert!(output.starts_with("\"TestVdf\"\n{"), "output: {}", output);
    // The closing brace may or may not have a trailing newline depending on
    // whether the last element is a nested object, so just check it ends with }
    assert!(output.trim_end().ends_with("}"), "output: {}", output);
    // Verify content is present (order may vary due to HashMap)
    assert!(output.contains("\"nested\""), "output: {}", output);
    assert!(
        output.contains("\"simple\"\t\"string\""),
        "output: {}",
        output
    );
    assert!(
        output.contains("\"value\"\t\"found\""),
        "output: {}",
        output
    );
    assert!(output.contains("\"number\"\t\"42\""), "output: {}", output);
}

#[test]
fn test_value_display_produces_vdf_format() {
    // Display always produces valid VDF text format
    let value = Value::Str(Cow::Borrowed("test"));
    assert_eq!(format!("{}", value), "\"test\"");
    // Alternate format is the same
    assert_eq!(format!("{:#}", value), "\"test\"");
}

#[test]
fn test_obj_display_produces_vdf_format() {
    let mut obj = Obj::new();
    obj.insert("k", "v".into());

    // Display produces VDF format with newlines and proper quoting
    let output = format!("{}", obj);
    assert!(output.contains('\n'));
    assert!(output.contains("\"k\""));
    assert!(output.contains("\"v\""));
    // Alternate format is the same
    assert_eq!(format!("{:#}", obj), output);
}
