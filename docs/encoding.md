# Text encoding

The optional ICU adapter and `is_valid_utf(input)` are available since `1.1.0`.
See the [README](../README.md) for core library integration and the
[changelog](../CHANGELOG.md) for upgrade notes.

Jieba's text entry points accept UTF-8, UTF-16 and UTF-32 through their existing
character types. Convert other encodings outside Jieba before segmentation.
The optional `IcuCodec` converts explicitly labeled UTF-8, GBK and GB18030 input
to UTF-8 and converts UTF-8 back to those encodings. Jieba has no encoding selector
or decoder parameter. `GBK` and `GB18030` are explicit choices; there is no ambiguous
`GB` alias or separate GB2312 converter in this adapter.
`IcuCodec` provides static operations and cannot be instantiated. Pass the byte
view and its encoding directly; no input wrapper or decoder object is required.

For source-tree integration, add this repository to your application and link the
optional interface target. Configure the consuming project with ICU enabled:

```sh
cmake -S . -B build -DBUILD_TESTING=OFF -DCPPJIEBA_ENABLE_ICU=ON
```

```cmake
add_subdirectory(external/modern-cppjieba)
target_link_libraries(my_app PRIVATE neo_cppjieba::icu)
```

ICU development headers and the `uc` and `data` libraries must be installed. The option is off
by default; the core target and `neo/Jieba.hpp` do not require ICU.

For installed headers or a manual header copy, configure the header search path
and C++23 as described in the [README](../README.md#cmake-集成), then discover and
link ICU in the consuming project:

```cmake
find_package(ICU REQUIRED COMPONENTS uc data)
target_link_libraries(my_app PRIVATE ICU::uc ICU::data)
```

Installation copies headers and dictionaries; it does not export a CMake package
or the `neo_cppjieba::neo_cppjieba` and `neo_cppjieba::icu` targets. All public
headers, including `IcuCodec.hpp`, are installed even with `CPPJIEBA_ENABLE_ICU=OFF`.
The option enables the source-tree adapter target and conversion tests; manual
integration configures ICU directly in the application.

```cpp
#include "neo/Jieba.hpp"
#include "neo/Unicode.hpp"
#include "neo/encoding/IcuCodec.hpp"

#include <string>
#include <string_view>

using namespace neo_cppjieba;

const auto jieba = Jieba{"dict/jieba.dict.utf8", "dict/hmm_model.utf8", ""};
const auto bytes = std::string{"\xD6\xD0\xB9\xFA"}; // 中国 in GBK.
const auto utf8 = IcuCodec::to_utf8(bytes, EncodingId::GBK);
const auto result = jieba.cut(utf8, CutMode::MIX);
// result[0].word borrows utf8; its source range is [0, 6).

auto workspace = Workspace{};
const auto reused = jieba.cut_with_workspace(utf8, CutMode::SEARCH, workspace);

const auto utf8_result = jieba.cut_owned(IcuCodec::to_utf8(bytes, EncodingId::GBK), CutMode::MIX);
// utf8_result owns UTF-8 text; its source range is [0, 6).
```

Keep the converted UTF string alive and unchanged while using borrowed results.
Use `cut_owned()` to transfer the converted string into the result. Token source
offsets refer to the UTF text passed to Jieba, not to the original GBK bytes;
rune offsets still count Unicode scalars. For example, 中国 occupies four GBK
bytes and six UTF-8 bytes. Original-byte location mapping belongs to the outer
conversion layer and is not carried by Jieba's results.

The conversion API provides the following entry points:

| Purpose | Interface | Result |
| --- | --- | --- |
| Convert to UTF-8 | `to_utf8(bytes, encoding)` | Owned UTF-8 string |
| Convert from UTF-8 | `from_utf8(utf8, encoding)` | Owned string in the target encoding |
| Locate runes in original bytes | `decode_with_offset_into(bytes, encoding, decoded)` | Runes and source byte boundaries |
| Locate runes in newly encoded bytes | `from_utf8_with_offset(utf8, encoding)` | `EncodedTextWithOffset` containing `bytes` and `offsets` |

```cpp
const auto gbk = IcuCodec::from_utf8(utf8, EncodingId::GBK);
const auto gb18030 = IcuCodec::from_utf8(utf8, EncodingId::GB18030);
const auto encoded = IcuCodec::from_utf8_with_offset(utf8, EncodingId::GBK);
const auto range = result[0].position.runes;
const auto begin = encoded.offsets[range.begin];
const auto end = encoded.offsets[range.end];
const auto gbk_word = std::string_view{encoded.bytes}.substr(begin, end - begin);
```

`EncodedTextWithOffset::offsets[i]` is the byte position of rune boundary `i` in
`bytes`, not in the UTF-8 input. For `A中B` encoded as GBK it is `{0, 1, 3, 4}`;
the UTF-8 boundaries are `{0, 1, 4, 5}`. A GB18030 supplementary character still
contributes one rune and one boundary, although ICU uses two UTF-16 units internally.
The table contains rune count plus one entries, ends at `bytes.size()`, and is `{0}`
for empty input. As with the existing decoder, mapped sizes must fit the configured
`SourceOffset` type. Plain `from_utf8` allocates no rune or offset buffers.

Use rune ranges from segmentation of the same, unchanged text. UTF-8 byte offsets
from `TokenPosition::source` cannot index GBK or GB18030 output. The mapped result
owns its bytes and offset table; views such as `gbk_word` borrow that result.

For exact original-byte slices, retain the source mapping instead of re-encoding:

```cpp
auto decoded = UnicodeWithOffset{};
IcuCodec::decode_with_offset_into(bytes, EncodingId::GBK, decoded);
const auto mapped_utf8 = encode<char>(decoded.runes);
const auto mapped_result = jieba.cut(mapped_utf8, CutMode::SEARCH);
const auto original_range = mapped_result[0].position.runes;
const auto original_begin = decoded.offsets[original_range.begin];
const auto original_end = decoded.offsets[original_range.end];
const auto original_word = std::string_view{bytes}.substr(original_begin, original_end - original_begin);
```

This preserves the actual source bytes even when converter tables contain aliases.
Re-encoding chooses the installed ICU mapping and does not promise byte-identical
round trips. Keep the original bytes alive and unchanged while using their slices.

Conversion borrows input only for the duration of the call and returns owned
storage. A temporary string can therefore be passed directly to a static conversion
operation. Jieba's borrowed token results still require the converted UTF input to
stay alive, as described above.

`IcuCodec::decode_with_offset_into(bytes, encoding, decoded)` also exposes the existing
`UnicodeWithOffset` pipeline. On success the offset table has one boundary per
rune plus the final source length. Empty input produces the single boundary zero.
Malformed input throws through `LogConfig::Exception`, with the encoding and
source offset in the diagnostic. A failed decode may leave partial output;
the next call clears and reuses the buffers. There is no encoding detection,
replacement, normalization or BOM removal.

Both `from_utf8` forms validate UTF-8 through the native checked decoder, including
when the target is UTF-8. Malformed UTF-8 and characters unavailable in the target
encoding throw through `LogConfig::Exception`. Encoding diagnostics identify the
target, scalar and UTF-8 byte offset. For example, `中A😀` fails at UTF-8 byte 4 when
targeting GBK; GB18030 supports that supplementary scalar. Embedded NUL does not
terminate conversion. A failed call returns no partial result.

`is_valid_utf(input)` is an optional preflight check in `neo/Unicode.hpp`:

```cpp
const auto valid = is_valid_utf(utf8);
const auto valid_utf16 = is_valid_utf(std::u16string_view{u"中国😀"});
```

It checks the UTF encoding associated with the input's code-unit type: `char`,
`char8_t` and byte buffers mean UTF-8; `char16_t` means UTF-16; `char32_t` means
UTF-32. `wchar_t` follows its native width. Empty text is valid. Malformed
sequences return false. The scan is linear and allocates no decoding buffers.

Jieba already performs strict validation while decoding text before segmentation.
Malformed input throws through the existing error path, including in release
builds. Calling `is_valid_utf()` before every cut would scan the input twice;
ordinary calls can rely on the fused decoding check. A successful preflight does
not authorize later mutations: every text call validates its current input.
The low-level `cut_runes` interface retains its precondition of already decoded
Unicode scalars; use the UTF-32 text entry point to validate untrusted code points.

Validation establishes well-formedness, not the original encoding. Bytes C2 A9
are legal in both UTF-8 and GBK, with different meanings. ASCII is also shared
by both. Even `char8_t` storage can contain malformed bytes. Source encoding must
come from the caller, file format or protocol; do not use a successful UTF-8 check
as automatic encoding detection or a failed check as proof of GBK.

UTF-8 uses the native decoder. GBK and GB18030 use ICU's named converters with
STOP callbacks in both directions and optional fallback mappings disabled. The
adapter does not substitute or silently skip unrepresentable characters. ICU still
applies its documented private-use and reverse fallback mappings regardless of that
flag, so this setting is not a round-trip guarantee. See the
[ICU converter API](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ucnv_8h.html)
and [conversion guide](https://unicode-org.github.io/icu/userguide/conversion/converters.html).
Each call owns its converter, so the codec has no shared mutable state. Mapping
revisions follow the installed ICU data; pin ICU when deployments need identical
mappings, particularly across GB18030 revisions.

The `<unicode/...>` headers are supplied by ICU4C, not the C++ standard library
or a vendored copy in this repository. CMake's `FindICU` locates an installed ICU;
`ICU::uc` and `ICU::data` carry its include paths and platform library locations.
Configuration reports the resolved version and header directory. Use `ICU_ROOT`
to select a particular installation, including when it is outside the standard
system paths. See the [CMake FindICU documentation](https://cmake.org/cmake/help/latest/module/FindICU.html).

ICU supports Linux, macOS and Windows. This adapter uses its C conversion API
and exposes standard C++ strings and existing Unicode containers. Portable source
code still needs matching headers, libraries, architecture and runtime deployment
on each target. In particular, ICU recommends shipping the matching DLLs with a
Windows application. See the [ICU4C guide](https://unicode-org.github.io/icu/userguide/icu4c/).

This project currently discovers ICU without locking a version. Reproducible
conversion requires pinning both the ICU release and its conversion data through
the consuming project's dependency manager or controlled installation, then
running the conversion tests on each supported platform. ABI compatibility and
mapping compatibility are separate: ICU 73.2 changed GB18030 conversion tables
for GB18030-2022 support. A successful build against an older ICU does not prove
those newer mappings. See [ICU versioning](https://unicode-org.github.io/icu/userguide/icu/design.html)
and the [ICU 73.2 release notes](https://icu.unicode.org/download/73).

Additional backends only need to produce a valid UTF string before calling Jieba.
They require no changes to its facade or segmentation algorithms. An outer decoder
that exposes original-byte offsets must still define how its mapping handles
stateful encodings or one byte sequence producing several Unicode scalars.

This version keeps dictionaries and models in UTF-8 and supports owned conversion
between UTF-8 and GBK/GB18030, with optional rune-to-byte location mapping.
The `encoded_text_test.cpp` unit tests are registered when ICU is enabled.

To run the modern unit suite with conversion tests included:

```sh
cmake -S . -B build-icu -DCMAKE_BUILD_TYPE=Debug \
  -DBUILD_TESTING=ON -DCPPJIEBA_ENABLE_ICU=ON
cmake --build build-icu --config Debug --target test_neo.run --parallel
ctest --test-dir build-icu -C Debug -R '^neo_unit_tests$' --output-on-failure
```
