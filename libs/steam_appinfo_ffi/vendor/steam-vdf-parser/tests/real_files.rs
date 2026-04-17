//! Integration tests against real Steam VDF files.

use std::path::Path;
use steam_vdf_parser::{parse_binary, parse_packageinfo, parse_text};

#[test]
fn test_parse_real_localconfig_text() {
    let path = Path::new("tests/fixtures/localconfig.vdf");
    let content = std::fs::read_to_string(path).expect("Failed to read localconfig.vdf");

    let result = parse_text(&content);
    assert!(
        result.is_ok(),
        "Failed to parse localconfig.vdf: {:?}",
        result.err()
    );

    let vdf = result.unwrap();
    assert_eq!(vdf.key(), "UserLocalConfigStore");

    let obj = vdf.as_obj().expect("Root should be an object");
    assert!(!obj.is_empty(), "Root should have keys");

    // Check for known keys that should exist in localconfig
    assert!(obj.get("Broadcast").is_some());
    assert!(obj.get("friends").is_some());
}

#[test]
fn test_parse_real_appinfo_binary() {
    let path = Path::new("tests/fixtures/appinfo_10.vdf");
    let data = std::fs::read(path).expect("Failed to read appinfo_10.vdf");

    let result = parse_binary(&data);
    assert!(
        result.is_ok(),
        "Failed to parse appinfo_10.vdf: {:?}",
        result.err()
    );

    let vdf = result.unwrap();
    assert!(vdf.key().starts_with("appinfo_universe_"));

    let obj = vdf.as_obj().expect("Root should be an object");
    assert!(!obj.is_empty(), "Root should have keys");

    // appinfo_10.vdf should contain 10 apps (numeric keys as strings)
    assert_eq!(
        obj.len(),
        10,
        "Should have exactly 10 apps in appinfo_10.vdf"
    );
}

#[test]
fn test_parse_real_packageinfo_binary() {
    let path = Path::new("tests/fixtures/packageinfo.vdf");
    let data = std::fs::read(path).expect("Failed to read packageinfo.vdf");

    let result = parse_packageinfo(&data);
    assert!(
        result.is_ok(),
        "Failed to parse packageinfo.vdf: {:?}",
        result.err()
    );

    let vdf = result.unwrap();
    assert!(vdf.key().starts_with("packageinfo_universe_"));

    let obj = vdf.as_obj().expect("Root should be an object");
    assert!(!obj.is_empty(), "Root should have keys");

    // packageinfo.vdf should contain packages
    // Check that we have at least some entries
    assert!(
        obj.len() > 50,
        "Should have many packages in packageinfo.vdf"
    );

    // Check that package 0 exists and has the expected metadata
    let pkg0 = obj.get("0").expect("Package 0 should exist");
    let pkg0_obj = pkg0.as_obj().expect("Package 0 should be an object");

    // Check metadata fields
    assert_eq!(
        pkg0_obj.get("packageid").and_then(|v| v.as_i32()),
        Some(0),
        "Package 0 should have packageid = 0"
    );

    assert!(
        pkg0_obj
            .get("change_number")
            .and_then(|v| v.as_u64())
            .is_some(),
        "Package 0 should have change_number"
    );

    assert!(
        pkg0_obj.get("sha1").and_then(|v| v.as_str()).is_some(),
        "Package 0 should have sha1 hash"
    );

    // Check that the VDF data is present under the "0" key
    let vdf_data = pkg0_obj
        .get("0")
        .expect("Package 0 should have VDF data under '0' key");
    let vdf_obj = vdf_data.as_obj().expect("VDF data should be an object");

    // Check for known fields in the VDF data
    assert!(
        vdf_obj.get("packageid").is_some(),
        "VDF data should contain packageid"
    );
    assert!(
        vdf_obj.get("billingtype").is_some(),
        "VDF data should contain billingtype"
    );
    assert!(
        vdf_obj.get("licensetype").is_some(),
        "VDF data should contain licensetype"
    );
}
