//! Text VDF format parser.
//!
//! Parses human-readable VDF text format using winnow parser combinators.

pub mod parser;

pub use parser::parse as parse_text;
