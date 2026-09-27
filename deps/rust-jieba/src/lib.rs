use jieba_rs::{HmmModel, Jieba, Token};
use std::fs::File;
use std::hint::black_box;
use std::io::BufReader;
use std::slice;
use std::str;
use std::time::Instant;

/// A borrowed UTF-8 input; the caller keeps its storage alive for the entire call.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct Input {
    data: *const u8,
    len: usize,
}

/// A result word either borrows the input or owns an exactly sized byte allocation.
#[repr(C)]
pub struct Word {
    data: *const u8,
    len: usize,
    owned: bool,
}

/// Owns the result descriptors; borrowed words remain valid only while the input lives.
#[repr(C)]
pub struct Words {
    words: *mut Word,
    len: usize,
}

/// Native timing includes result destruction; token counts check that every round ran.
#[repr(C)]
pub struct Measurement {
    pub milliseconds: f64,
    pub tokens: usize,
}

/// Interpret caller-owned bytes without extending their lifetime beyond the call.
unsafe fn text<'a>(input: Input) -> Result<&'a str, str::Utf8Error> {
    if input.len == 0 {
        return Ok("");
    }
    str::from_utf8(unsafe { slice::from_raw_parts(input.data, input.len) })
}

/// Dispatch the same four public operations used by the C++ benchmark.
fn cut<'a>(jieba: &Jieba, input: &'a str, method: u32) -> Vec<Token<'a>> {
    match method {
        0 => jieba.cut(input, true),
        1 => jieba.cut(input, false),
        2 => jieba.cut_all(input),
        3 => jieba.cut_for_search(input, true),
        _ => unreachable!("invalid benchmark method"),
    }
}

/// Construct both dictionaries and runtime HMM models from the same files as C++.
/// # Safety
/// Both inputs must reference live byte buffers for their declared lengths.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_bench_new(dict: Input, model: Input) -> *mut Jieba {
    let load = || -> Result<Jieba, Box<dyn std::error::Error>> {
        let dict = unsafe { text(dict)? };
        let model = unsafe { text(model)? };
        let mut jieba = Jieba::with_dict(&mut BufReader::new(File::open(dict)?))?;
        jieba.set_hmm_model(HmmModel::load(&mut BufReader::new(File::open(model)?))?);
        Ok(jieba)
    };
    match load() {
        Ok(jieba) => Box::into_raw(Box::new(jieba)),
        Err(error) => {
            eprintln!("Rust benchmark initialization: {error}");
            std::ptr::null_mut()
        }
    }
}

/// Release an engine after all operations using it have finished.
/// # Safety
/// The handle must be returned by rust_bench_new and freed exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_bench_free(jieba: *mut Jieba) {
    if !jieba.is_null() {
        drop(unsafe { Box::from_raw(jieba) });
    }
}

/// Pack tokens using either borrowed views or the upstream C API's per-word copies.
fn pack(tokens: Vec<Token<'_>>, copy_words: bool) -> *mut Words {
    let words: Vec<Word> = tokens
        .into_iter()
        .map(|token| {
            let len = token.word.len();
            let data = if copy_words {
                Box::into_raw(token.word.to_owned().into_boxed_str()) as *const u8
            } else {
                token.word.as_ptr()
            };
            Word {
                data,
                len,
                owned: copy_words,
            }
        })
        .collect();
    let len = words.len();
    let words = Box::into_raw(words.into_boxed_slice()) as *mut Word;
    Box::into_raw(Box::new(Words { words, len }))
}

/// Return descriptors for correctness checks and end-to-end C++ output timings.
/// # Safety
/// The engine and input must be live; method is 0..=3. Free the result before input.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_bench_cut(
    jieba: *const Jieba,
    input: Input,
    method: u32,
    copy_words: bool,
) -> *mut Words {
    let Ok(input) = (unsafe { text(input) }) else {
        return std::ptr::null_mut();
    };
    pack(cut(unsafe { &*jieba }, input, method), copy_words)
}

/// Free the descriptor array and only those word buffers owned by Rust.
/// # Safety
/// The argument must be a live result from rust_bench_cut, released exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_bench_words_free(words: *mut Words) {
    let words = unsafe { Box::from_raw(words) };
    let descriptors =
        unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(words.words, words.len)) };
    for word in descriptors.iter().filter(|word| word.owned) {
        drop(unsafe {
            Box::from_raw(std::ptr::slice_from_raw_parts_mut(
                word.data as *mut u8,
                word.len,
            ))
        });
    }
}

/// Measure a complete corpus in Rust, with no per-line C ABI or C++ result conversion.
fn measure(jieba: &Jieba, lines: &[&str], method: u32, owned: bool, rounds: usize) -> Measurement {
    let mut tokens = 0;
    let start = Instant::now();
    for _ in 0..rounds {
        for line in lines {
            let words = cut(jieba, black_box(line), method);
            tokens += words.len();
            if owned {
                let words: Vec<String> =
                    words.into_iter().map(|word| word.word.to_owned()).collect();
                black_box(words);
            } else {
                black_box(words);
            }
        }
    }
    Measurement {
        milliseconds: start.elapsed().as_secs_f64() * 1000.0,
        tokens,
    }
}

/// Validate UTF-8 and construct corpus views before starting the native timer.
/// # Safety
/// Engine, array, and input buffers must remain live throughout the call; method is 0..=3.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn rust_bench_measure(
    jieba: *const Jieba,
    lines: *const Input,
    len: usize,
    method: u32,
    owned: bool,
    rounds: usize,
) -> Measurement {
    let lines = if len == 0 {
        &[]
    } else {
        unsafe { slice::from_raw_parts(lines, len) }
    };
    let lines: Result<Vec<&str>, _> = lines.iter().map(|line| unsafe { text(*line) }).collect();
    match lines {
        Ok(lines) => measure(unsafe { &*jieba }, &lines, method, owned, rounds),
        Err(_) => Measurement {
            milliseconds: -1.0,
            tokens: 0,
        },
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    /// A small deterministic dictionary keeps adapter tests independent of repository data.
    fn engine() -> Jieba {
        Jieba::with_dict(&mut Cursor::new("中国 10 ns\n人民 10 n\n中国人民 1 n\n")).unwrap()
    }

    /// Extract owned strings before releasing either representation of the FFI result.
    unsafe fn collect(result: *mut Words) -> Vec<String> {
        let descriptors = unsafe { slice::from_raw_parts((*result).words, (*result).len) };
        let words = descriptors
            .iter()
            .map(|word| {
                unsafe {
                    text(Input {
                        data: word.data,
                        len: word.len,
                    })
                }
                .unwrap()
                .to_owned()
            })
            .collect();
        unsafe { rust_bench_words_free(result) };
        words
    }

    #[test]
    fn copied_and_borrowed_ffi_match_native_for_all_methods() {
        let jieba = engine();
        for line in ["", "中国人民", "中国，人民! Rust 123", "🙂\0中国\r\n"] {
            for method in 0..4 {
                let expected: Vec<_> = cut(&jieba, line, method)
                    .iter()
                    .map(|word| word.word.to_owned())
                    .collect();
                for copied in [false, true] {
                    let result = unsafe {
                        rust_bench_cut(
                            &jieba,
                            Input {
                                data: line.as_ptr(),
                                len: line.len(),
                            },
                            method,
                            copied,
                        )
                    };
                    assert_eq!(unsafe { collect(result) }, expected);
                }
            }
        }
    }

    #[test]
    fn native_measurement_counts_every_round_for_both_output_types() {
        let jieba = engine();
        let lines = ["中国人民", "Rust 123", ""];
        for method in 0..4 {
            let expected: usize = lines
                .iter()
                .map(|line| cut(&jieba, line, method).len())
                .sum();
            for owned in [false, true] {
                let result = measure(&jieba, &lines, method, owned, 3);
                assert_eq!(result.tokens, expected * 3);
                assert!(result.milliseconds >= 0.0);
            }
        }
    }

    #[test]
    fn empty_null_inputs_are_valid() {
        let jieba = engine();
        let input = Input {
            data: std::ptr::null(),
            len: 0,
        };
        for method in 0..4 {
            for copied in [false, true] {
                let result = unsafe { rust_bench_cut(&jieba, input, method, copied) };
                assert!(unsafe { collect(result) }.is_empty());
            }
            let result =
                unsafe { rust_bench_measure(&jieba, std::ptr::null(), 0, method, false, 1) };
            assert_eq!(result.tokens, 0);
        }
    }

    #[test]
    fn invalid_utf8_returns_an_error_without_a_partial_result() {
        let jieba = engine();
        let invalid = [0xff];
        let input = Input {
            data: invalid.as_ptr(),
            len: invalid.len(),
        };
        assert!(unsafe { rust_bench_cut(&jieba, input, 0, false) }.is_null());
        assert!(unsafe { rust_bench_measure(&jieba, &input, 1, 0, false, 1) }.milliseconds < 0.0);
    }
}
