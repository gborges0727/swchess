#include "assets/piece_dll.h"

#include <cctype>
#include <stdexcept>

#include "assets/ne.h"

namespace swchess {

const char* const kPieceCodes[12] = {"AT", "BF", "C3", "CB", "DV", "EM",
                                     "LO", "LS", "R2", "SP", "ST", "YO"};

bool parsePieceResourceName(const std::string& name, std::string* direction, int* frame) {
    // The shape is two characters for the piece, an underscore, one or two
    // letters for the direction, then the digits of the frame number.
    std::size_t underscore = name.find('_');
    if (underscore != 2 || name.size() < 5) {
        return false;
    }
    std::size_t at = 3;
    std::string letters;
    while (at < name.size() && std::isupper(static_cast<unsigned char>(name[at])) != 0) {
        letters.push_back(name[at]);
        ++at;
    }
    if (letters.empty() || letters.size() > 2 || at >= name.size()) {
        return false;
    }
    int digits = 0;
    for (std::size_t i = at; i < name.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(name[i])) == 0) {
            return false;
        }
        digits = digits * 10 + (name[i] - '0');
    }
    if (direction != nullptr) {
        *direction = letters;
    }
    if (frame != nullptr) {
        *frame = digits;
    }
    return true;
}

PieceDll loadPieceDll(const std::string& dir, const std::string& piece) {
    PieceDll out;
    out.piece = piece;
    out.source = piece + ".DLL";
    std::string path = dir + "/" + out.source;
    std::vector<std::uint8_t> blob = readBinaryFile(path);
    std::vector<NeResource> resources = readNeResources(blob);

    for (const NeResource& resource : resources) {
        if (!resource.type.isNumeric || resource.type.id != kRtBitmap) {
            continue;
        }
        PieceBitmap bitmap;
        bitmap.resourceName = resource.name.text();
        bitmap.resourceOffset = resource.offset;
        bitmap.resourceLength = resource.length;
        parsePieceResourceName(bitmap.resourceName, &bitmap.direction, &bitmap.frame);
        bitmap.record = decodeRleRecord(blob, resource.offset, resource.offset + resource.length);
        bitmap.record.offset = static_cast<std::uint32_t>(resource.offset);
        if (!bitmap.record.complete) {
            throw std::runtime_error(out.source + " resource " + bitmap.resourceName +
                                     " decoded short");
        }
        out.bitmaps.push_back(std::move(bitmap));
    }
    return out;
}

std::vector<std::uint8_t> pieceToRGBA(const PieceBitmap& bitmap) {
    return anxToRGBA(bitmap.record);
}

}  // namespace swchess
