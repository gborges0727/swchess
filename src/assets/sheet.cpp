#include "assets/sheet.h"

#include <stdexcept>

#include "assets/cdfs.h"
#include "assets/ini.h"

namespace swchess {
namespace {

// Checks one separator column over the grid height only. The spare canvas below
// the grid in FACING_P.BMP is not part of the sheet.
SheetSeparator checkColumn(const IndexedBitmap& bitmap, int x, int limit) {
    SheetSeparator out;
    out.position = x;
    int height = limit < bitmap.height ? limit : bitmap.height;
    out.index = bitmap.indexAt(x, 0);
    out.flat = true;
    for (int y = 0; y < height; ++y) {
        if (bitmap.indexAt(x, y) != out.index) {
            out.flat = false;
            break;
        }
    }
    return out;
}

// Checks one separator row over the grid width only.
SheetSeparator checkRow(const IndexedBitmap& bitmap, int y, int limit) {
    SheetSeparator out;
    out.position = y;
    int width = limit < bitmap.width ? limit : bitmap.width;
    out.index = bitmap.indexAt(0, y);
    out.flat = true;
    for (int x = 0; x < width; ++x) {
        if (bitmap.indexAt(x, y) != out.index) {
            out.flat = false;
            break;
        }
    }
    return out;
}

}  // namespace

const char* const kSheetKeys[4] = {"2DSET_", "WHTBTM_", "WHTTOP_", "FACING_"};

std::string sheetFileName(const std::string& key) {
    return key + "P.BMP";
}

PieceSheet loadPieceSheet(const std::string& dir, const std::string& key) {
    IniFile cm(resolveCdFile(dir, "CM.INI").string());
    const IniSection* chesssets = cm.section("chesssets");
    if (chesssets == nullptr) {
        throw std::runtime_error("CM.INI has no [chesssets] section");
    }

    PieceSheet sheet;
    sheet.key = key;
    sheet.source = sheetFileName(key);
    sheet.cmIniValue = chesssets->get(key);
    // The line reads label, dimension, palette flag, class, cell width, cell
    // height, then a scale number. The two cell sizes are tokens 5 and 6.
    std::vector<std::string> tokens = iniTokens(sheet.cmIniValue);
    if (tokens.size() < 6) {
        throw std::runtime_error("CM.INI [chesssets] carries no cell size for " + key);
    }
    sheet.cellWidth = iniAsInt(tokens[4], 0);
    sheet.cellHeight = iniAsInt(tokens[5], 0);
    if (sheet.cellWidth <= 0 || sheet.cellHeight <= 0) {
        throw std::runtime_error("CM.INI [chesssets] cell size does not read as a number for " +
                                 key);
    }

    IndexedBitmap bitmap = loadBmpIndexed(resolveCdFile(dir, sheet.source).string());
    sheet.sheetWidth = bitmap.width;
    sheet.sheetHeight = bitmap.height;

    int gridWidth = kSheetColumns * (sheet.cellWidth + 1);
    int gridHeight = kSheetRows * (sheet.cellHeight + 1);
    sheet.separatorsFlat = true;
    for (int c = 0; c < kSheetColumns; ++c) {
        int x = c * (sheet.cellWidth + 1);
        if (x >= bitmap.width) {
            continue;
        }
        SheetSeparator separator = checkColumn(bitmap, x, gridHeight);
        sheet.separatorsFlat = sheet.separatorsFlat && separator.flat;
        sheet.columnSeparators.push_back(separator);
    }
    for (int r = 0; r < kSheetRows; ++r) {
        int y = r * (sheet.cellHeight + 1);
        if (y >= bitmap.height) {
            continue;
        }
        SheetSeparator separator = checkRow(bitmap, y, gridWidth);
        sheet.separatorsFlat = sheet.separatorsFlat && separator.flat;
        sheet.rowSeparators.push_back(separator);
    }

    for (int r = 0; r < kSheetRows; ++r) {
        for (int c = 0; c < kSheetColumns; ++c) {
            SheetCell cell;
            cell.row = r;
            cell.column = c;
            cell.x = 1 + c * (sheet.cellWidth + 1);
            cell.y = 1 + r * (sheet.cellHeight + 1);
            cell.width = sheet.cellWidth;
            cell.height = sheet.cellHeight;
            if (cell.x + cell.width > bitmap.width || cell.y + cell.height > bitmap.height) {
                continue;
            }
            cell.rgba = bmpRegionToRGBA(bitmap, cell.x, cell.y, cell.width, cell.height, 0);
            sheet.cells.push_back(std::move(cell));
        }
    }
    return sheet;
}

}  // namespace swchess
