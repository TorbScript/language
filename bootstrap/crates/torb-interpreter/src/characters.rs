//! What a `Char` answers about itself: the two case mappings and the three questions.
//!
//! This is `torb_char_*` of `runtime/text.c`, function for function, and a change to one is a change to the other -
//! `Char.toUpperCase`, `Char.isLetter` and their siblings are observable, so the conformance suite compares them. Rust's
//! own `char` methods are the *full* Unicode tables and are deliberately not used here: a compiled program would then
//! have to carry those tables, which is milestone 8's, and until then the two implementations would disagree on every
//! character above ASCII.
//!
//! The rules the language promises are in `runtime/README.md` ("What is deliberately approximate"): classification and
//! case mapping are ASCII plus the letters of Latin-1, and a code point the mapping does not cover is answered
//! unchanged.

/// The simple uppercase of one code point, or the code point itself where there is none.
///
/// A `Char` is one code point and answers one, so only a one-to-one mapping applies: `ß` is answered unchanged, because
/// its uppercase is `SS` and that is two code points. `String.toUpperCase` has the same limit and for the same reason.
pub fn to_upper_case(character: char) -> char {
    let code = character as u32;
    let mapped = match code {
        0x61..=0x7A => code - 0x20,
        // The letters of Latin-1, without `÷` (a symbol) and without `ß` (no single uppercase code point)
        0xE0..=0xFE if code != 0xF7 => code - 0x20,
        // The one Latin-1 letter whose partner lies above Latin-1: `ÿ` to `Ÿ`
        0xFF => 0x178,
        _ => code,
    };
    char::from_u32(mapped).unwrap_or(character)
}

/// The simple lowercase of one code point, or the code point itself where there is none.
pub fn to_lower_case(character: char) -> char {
    let code = character as u32;
    let mapped = match code {
        0x41..=0x5A => code + 0x20,
        // The letters of Latin-1, without `×` (a symbol)
        0xC0..=0xDE if code != 0xD7 => code + 0x20,
        0x178 => 0xFF,
        _ => code,
    };
    char::from_u32(mapped).unwrap_or(character)
}

/// Whether the character is one of `0` to `9`. Only those: a digit is what `Int.tryFrom` reads.
pub fn is_digit(character: char) -> bool {
    character.is_ascii_digit()
}

/// Whether the character is a letter, exactly for ASCII and by Latin-1's own division above it.
pub fn is_letter(character: char) -> bool {
    let code = character as u32;
    if character.is_ascii_alphabetic() {
        return true;
    }
    if code < 0x80 {
        return false;
    }
    // An approximation above ASCII: the punctuation and the symbols of Latin-1 are not letters, everything else is
    if code <= 0xBF {
        return code == 0xAA || code == 0xB5 || code == 0xBA;
    }
    code != 0xD7 && code != 0xF7
}

/// Whether the character is whitespace: ASCII's, plus the `White_Space` code points a text really carries.
pub fn is_whitespace(character: char) -> bool {
    let code = character as u32;
    if code < 0x80 {
        return matches!(character, ' ' | '\t' | '\n' | '\r' | '\u{b}' | '\u{c}');
    }
    code == 0x85
        || code == 0xA0
        || code == 0x1680
        || (0x2000..=0x200A).contains(&code)
        || code == 0x2028
        || code == 0x2029
        || code == 0x202F
        || code == 0x205F
        || code == 0x3000
}

/// Every character of a text mapped, which is [to_upper_case] per character and nothing more.
pub fn mapped_case(text: &str, upper: bool) -> String {
    text.chars().map(|character| if upper { to_upper_case(character) } else { to_lower_case(character) }).collect()
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The pairs `runtime/text.c` maps, and the characters it deliberately leaves alone.
    #[test]
    fn the_case_mapping_is_ascii_and_the_letters_of_latin_1() {
        let pairs = [('a', 'A'), ('z', 'Z'), ('ä', 'Ä'), ('ö', 'Ö'), ('à', 'À'), ('þ', 'Þ'), ('ÿ', 'Ÿ')];
        for (lower, upper) in pairs {
            assert_eq!(to_upper_case(lower), upper, "the uppercase of {lower}");
            assert_eq!(to_lower_case(upper), lower, "the lowercase of {upper}");
        }
        // No single uppercase code point, a symbol, and above the mapping
        for unchanged in ['ß', '×', '÷', 'α', 'Ω', 'あ', '1', '_'] {
            assert_eq!(to_upper_case(unchanged), unchanged, "the uppercase of {unchanged}");
            assert_eq!(to_lower_case(unchanged), unchanged, "the lowercase of {unchanged}");
        }
    }

    #[test]
    fn a_text_is_mapped_character_by_character() {
        assert_eq!(mapped_case("straße", true), "STRAßE");
        assert_eq!(mapped_case("Grüße", false), "grüße");
        assert_eq!(mapped_case("naïve", true), "NAÏVE");
    }
}
