// Slicer for the four piece sheets: 2DSET_P.BMP, WHTBTM_P.BMP, WHTTOP_P.BMP
// and FACING_P.BMP.
//
// Each sheet holds twelve characters in two rows of six. CM.INI names the
// sheets in its [chesssets] section and gives the cell size. The line
// `WHTBTM_=WhiteOnBottom 3 P S 68 142 2` means 68 by 142 cells. A one pixel
// separator sits in front of every column and every row, so cell k starts at
// 1 + k * (cell size + 1) on both axes.
//
// FACING_P.BMP is a 640 by 480 canvas holding the same 414 by 286 grid in its
// top-left corner. The rest of that canvas is not part of any cell.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "assets/bmp.h"

namespace swchess {

// The number of cells across and down every sheet.
constexpr int kSheetColumns = 6;
constexpr int kSheetRows = 2;

// One cell cut out of a sheet, in top-down RGBA8 with index 0 fully clear.
struct SheetCell {
    int row = 0;
    int column = 0;
    int x = 0;  // left edge inside the sheet
    int y = 0;  // top edge inside the sheet
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
};

// One separator line and whether every pixel along it holds the same index.
struct SheetSeparator {
    int position = 0;         // x for a column line, y for a row line
    bool flat = false;        // the whole line over the grid is one index
    std::uint8_t index = 0;   // the index found at the start of the line
};

// One sheet with its twelve cells.
struct PieceSheet {
    std::string key;     // "WHTBTM_", the CM.INI key
    std::string source;  // "WHTBTM_P.BMP"
    std::string cmIniValue;  // the raw [chesssets] line
    int sheetWidth = 0;
    int sheetHeight = 0;
    int cellWidth = 0;
    int cellHeight = 0;
    std::vector<SheetSeparator> columnSeparators;
    std::vector<SheetSeparator> rowSeparators;
    bool separatorsFlat = false;
    std::vector<SheetCell> cells;  // row 0 left to right, then row 1
};

// The four CM.INI keys, in the order the plan lists the sheets.
extern const char* const kSheetKeys[4];

// Returns the BMP file name for one CM.INI key, such as "WHTBTM_P.BMP".
std::string sheetFileName(const std::string& key);

// Reads CM.INI and the sheet for `key` out of `dir`, then cuts the twelve
// cells. Throws std::runtime_error when a file is missing or CM.INI carries no
// cell size for that key.
PieceSheet loadPieceSheet(const std::string& dir, const std::string& key);

}  // namespace swchess
