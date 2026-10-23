#include "neo/encoding/IcuCodec.hpp"

#include <string>
#include <string_view>

// Verify that the installed ICU target supplies the codec's include and link dependencies.
int main() {
    const auto text =
        neo_cppjieba::IcuCodec::to_utf8(std::string_view{"\xD6\xD0\xB9\xFA"}, neo_cppjieba::EncodingId::GBK);
    return text == "\xE4\xB8\xAD\xE5\x9B\xBD" ? 0 : 1;
}
