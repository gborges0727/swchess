// Checks the walk sequences of all twelve pieces against the piece DLLs and
// runs WalkPlayer over them. Run it with the CD directory as the only
// argument.

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <set>
#include <string>
#include <vector>

#include "anim/walk.h"
#include "assets/piece_dll.h"

namespace {

int failures = 0;

void check(bool ok, const std::string& what) {
    if (!ok) {
        std::fprintf(stderr, "FAIL %s\n", what.c_str());
        ++failures;
    }
}

// Runs the player from the start to past the end in fixed steps and collects
// the step index of every frame it draws, in the order it draws them.
std::vector<std::size_t> sweep(const swchess::anim::WalkSequence& walk, double stepMs) {
    swchess::anim::WalkPlayer player;
    player.start(&walk, 0, 0, 0, 0, 0);
    std::vector<std::size_t> drawn;
    double now = 0.0;
    const std::int64_t limit = player.durationMs() + 500;
    while (true) {
        swchess::anim::WalkUpdate update = player.advance(static_cast<std::int64_t>(now));
        if (update.draw.visible && (drawn.empty() || drawn.back() != update.draw.stepIndex)) {
            drawn.push_back(update.draw.stepIndex);
        }
        if (static_cast<std::int64_t>(now) > limit) {
            break;
        }
        now += stepMs;
    }
    return drawn;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: walk_test <cd directory>\n");
        return 2;
    }
    const std::string cdDir = argv[1];

    try {
        std::printf("%-6s %-4s %5s %5s %6s %6s %6s\n", "piece", "dir", "steps", "keys", "first",
                    "totdx", "totdy");
        std::size_t sequencesWithSteps = 0;
        std::size_t stepsChecked = 0;
        std::vector<std::string> mismatches;

        for (const char* const* code = swchess::kPieceCodes;
             code < swchess::kPieceCodes + 12; ++code) {
            const std::string piece = *code;
            const swchess::PieceDll& dll = swchess::anim::sharedPieceDll(cdDir, piece);
            std::set<std::string> names;
            for (const swchess::PieceBitmap& bitmap : dll.bitmaps) {
                names.insert(bitmap.resourceName);
            }

            int totalByDirection[8][2] = {};
            std::size_t countByDirection[8] = {};

            for (std::size_t d = 0; d < 8; ++d) {
                const swchess::anim::Direction direction = swchess::anim::kDirections[d];
                const char* name = swchess::anim::directionName(direction);
                swchess::anim::WalkSequence walk = swchess::anim::loadWalk(cdDir, piece, direction);

                check(walk.piece == piece, piece + " " + name + " names its piece");
                check(walk.section == name, piece + " " + name + " names its section");
                check(walk.frameDelayMs == 120,
                      piece + " " + name + " takes the 120 ms frame delay from CM.INI");
                // The count rule: the loader keeps exactly `count` steps.
                check(walk.steps.size() == static_cast<std::size_t>(walk.declaredCount),
                      piece + " " + name + " keeps exactly its declared count of steps");
                check(walk.iniStepCount >= walk.steps.size(),
                      piece + " " + name + " lists at least as many keys as it declares");

                if (walk.extraKeysIgnored) {
                    char line[128];
                    std::snprintf(line, sizeof(line), "%s [%s] lists %zu steps and declares %d",
                                  piece.c_str(), name, walk.iniStepCount, walk.declaredCount);
                    mismatches.push_back(line);
                }

                int runX = 0;
                int runY = 0;
                for (const swchess::anim::WalkStep& step : walk.steps) {
                    check(names.count(step.resourceName) == 1,
                          piece + " " + name + " step " + step.key + " draws " +
                              step.resourceName + ", which the DLL holds");
                    check(step.bitmap != nullptr,
                          piece + " " + name + " step " + step.key + " points at its bitmap");
                    check(step.width > 0 && step.height > 0,
                          piece + " " + name + " step " + step.key + " has a sized bitmap");
                    runX += step.dx;
                    runY += step.dy;
                    check(step.cumulativeDx == runX && step.cumulativeDy == runY,
                          piece + " " + name + " step " + step.key + " sums its offsets");
                    ++stepsChecked;
                }
                check(walk.totalDx == runX && walk.totalDy == runY,
                      piece + " " + name + " reports the sum of its steps");

                totalByDirection[d][0] = walk.totalDx;
                totalByDirection[d][1] = walk.totalDy;
                countByDirection[d] = walk.steps.size();

                std::printf("%-6s %-4s %5zu %5zu %6s %6d %6d\n", piece.c_str(), name,
                            walk.steps.size(), walk.iniStepCount,
                            walk.steps.empty() ? "-" : walk.steps.front().key.c_str(),
                            walk.totalDx, walk.totalDy);

                if (walk.steps.empty()) {
                    continue;
                }
                ++sequencesWithSteps;

                // The player fires one frame per step at both sweep rates.
                const std::vector<std::size_t> fine = sweep(walk, 1.0);
                const std::vector<std::size_t> coarse = sweep(walk, 1000.0 / 60.0);
                check(fine.size() == walk.steps.size(),
                      piece + " " + name + " draws one frame per step at 1 ms");
                check(coarse.size() == walk.steps.size(),
                      piece + " " + name + " draws one frame per step at 16.67 ms");
                check(fine == coarse,
                      piece + " " + name + " draws the same frames at both sweep rates");
            }

            // North and south are mirror images in y only when their step
            // counts and their total dy agree in size. They do not, so this
            // reports the pair rather than failing the run.
            const std::size_t north = 0;  // kDirections[0] is N
            const std::size_t south = 4;  // kDirections[4] is S
            if (countByDirection[north] > 0 && countByDirection[south] > 0) {
                const int ny = totalByDirection[north][1];
                const int sy = totalByDirection[south][1];
                if (ny != -sy) {
                    std::printf("%s N and S are not mirror images in y: N sums %d, S sums %d\n",
                                piece.c_str(), ny, sy);
                } else {
                    std::printf("%s N and S mirror in y at %d\n", piece.c_str(), ny);
                }
            }

            // The rotation set turns the piece in place, so its frames come
            // from the [R] section and the "%s_R%03d" names.
            swchess::anim::WalkSequence rotation = swchess::anim::loadRotation(cdDir, piece);
            check(rotation.present, piece + " has an [R] section");
            check(rotation.steps.size() == static_cast<std::size_t>(rotation.declaredCount),
                  piece + " R keeps exactly its declared count of steps");
            for (const swchess::anim::WalkStep& step : rotation.steps) {
                check(names.count(step.resourceName) == 1,
                      piece + " R step " + step.key + " draws " + step.resourceName +
                          ", which the DLL holds");
            }
            std::printf("%-6s %-4s %5zu %5zu %6s %6d %6d\n", piece.c_str(), "R",
                        rotation.steps.size(), rotation.iniStepCount,
                        rotation.steps.empty() ? "-" : rotation.steps.front().key.c_str(),
                        rotation.totalDx, rotation.totalDy);
        }

        std::printf("checked %zu steps across %zu walks with frames\n", stepsChecked,
                    sequencesWithSteps);

        // AT.INI [W] is the mismatch the extractor names. It lists 16 numbered
        // keys and declares 15, and the loader drops the sixteenth.
        swchess::anim::WalkSequence atWest =
            swchess::anim::loadWalk(cdDir, "AT", swchess::anim::Direction::W);
        check(atWest.declaredCount == 15, "AT [W] declares 15 steps");
        check(atWest.iniStepCount == 16, "AT [W] lists 16 numbered keys");
        check(atWest.steps.size() == 15, "AT [W] plays 15 steps");
        check(atWest.extraKeysIgnored, "AT [W] reports that a key went unused");
        check(atWest.steps.back().key == "015", "AT [W] stops at key 015");
        check(atWest.steps.back().resourceName == "AT_W016", "AT [W] ends on AT_W016");
        check(atWest.steps.front().dx == -19 && atWest.steps.front().dy == -1,
              "AT [W] starts by moving 19 pixels left and 1 pixel up");

        // LO [S] and LO [NW] declare fewer steps than the DLL holds bitmaps.
        // The extra bitmaps never reach the screen.
        swchess::anim::WalkSequence loSouth =
            swchess::anim::loadWalk(cdDir, "LO", swchess::anim::Direction::S);
        check(loSouth.steps.size() == 14, "LO [S] plays 14 steps");
        check(loSouth.steps.front().resourceName == "LO_S004",
              "LO [S] starts on LO_S004 because its first key is 003");

        // The four sections the compass set leaves out belong to YO and R2.
        // The other three of the seven mismatches the extractor recorded live
        // in YO's [US], [UN], [DN] and [DS], which loadWalkSection reaches.
        for (const char* section : {"US", "UN", "DN", "DS"}) {
            swchess::anim::WalkSequence yo =
                swchess::anim::loadWalkSection(cdDir, "YO", section);
            check(yo.declaredCount == 15,
                  std::string("YO [") + section + "] declares 15 steps");
            check(yo.iniStepCount == 16, std::string("YO [") + section + "] lists 16 keys");
            check(yo.steps.size() == 15, std::string("YO [") + section + "] plays 15 steps");
            check(yo.extraKeysIgnored,
                  std::string("YO [") + section + "] reports that a key went unused");

            // R2's copies of the same sections number their keys 000, 002,
            // 004 and so on, so the loader must follow the file order.
            swchess::anim::WalkSequence r2 =
                swchess::anim::loadWalkSection(cdDir, "R2", section);
            check(r2.steps.size() == 15, std::string("R2 [") + section + "] plays 15 steps");
            check(r2.steps[1].keyNumber == 2,
                  std::string("R2 [") + section + "] takes 002 as its second step");
        }

        // DV.INI [E] writes "006=10" with no comma. The loader reads the 10 as
        // dx, leaves dy at 0, and says the value was malformed.
        swchess::anim::WalkSequence dvEast =
            swchess::anim::loadWalk(cdDir, "DV", swchess::anim::Direction::E);
        check(dvEast.steps.size() == 8, "DV [E] plays 8 steps");
        const swchess::anim::WalkStep& odd = dvEast.steps[3];
        check(odd.key == "006", "DV [E] step four is key 006");
        check(odd.malformed, "DV [E] key 006 holds one number and not two");
        check(odd.dx == 10 && odd.dy == 0, "DV [E] key 006 moves 10 right and 0 down");

        // A piece with no walk in a direction loads a sequence with no steps
        // rather than throwing. AT only walks along the four straight axes.
        swchess::anim::WalkSequence atNorthEast =
            swchess::anim::loadWalk(cdDir, "AT", swchess::anim::Direction::NE);
        check(atNorthEast.present, "AT has an [NE] section");
        check(atNorthEast.declaredCount == 0, "AT [NE] declares no steps");
        check(atNorthEast.steps.empty(), "AT [NE] plays nothing");

        // The player, checked against AT walking south.
        swchess::anim::WalkSequence atSouth =
            swchess::anim::loadWalk(cdDir, "AT", swchess::anim::Direction::S);
        swchess::anim::WalkPlayer player;
        player.start(&atSouth, 100, 50, 100, 114, 1000);
        check(player.durationMs() == 16 * 120, "AT [S] runs 16 steps at 120 ms each");
        check(!player.isFinished(), "the player is running once it starts");

        swchess::anim::WalkUpdate first = player.advance(1000);
        check(first.draw.visible, "the first step is on the screen at time zero");
        check(first.draw.stepIndex == 0, "the first step is step zero");
        check(first.draw.x == 100 && first.draw.y == 52,
              "AT [S] step one puts the piece two pixels below the start");

        swchess::anim::WalkUpdate second = player.advance(1000 + 120);
        check(second.draw.stepIndex == 1, "step two replaces step one at 120 ms");

        // Time never runs backwards, so an earlier call leaves the clock alone.
        swchess::anim::WalkUpdate back = player.advance(1000);
        check(back.draw.stepIndex == 1, "an earlier time leaves the step where it was");

        player.advance(1000 + player.durationMs());
        check(player.isFinished(), "the player finishes after the last frame delay");

        // Under the INI steps the piece lands where the file puts it, which is
        // 64 pixels down for AT walking south.
        check(atSouth.totalDy == 64, "AT [S] sums 64 pixels of downward movement");
        swchess::anim::WalkPlayer verbatim;
        verbatim.start(&atSouth, 100, 50, 100, 200, 0);
        check(verbatim.residualY() == 200 - (50 + 64),
              "the INI steps leave AT [S] short of a 150 pixel target");

        // Scaling stretches the same shape onto the target.
        swchess::anim::WalkPlayer scaledPlayer;
        scaledPlayer.setFit(swchess::anim::WalkFit::ScaleToTarget);
        scaledPlayer.start(&atSouth, 100, 50, 130, 200, 0);
        check(scaledPlayer.residualX() == 0 && scaledPlayer.residualY() == 0,
              "the scaled walk lands on the target exactly");
        check(scaledPlayer.positions().back().x == 130 &&
                  scaledPlayer.positions().back().y == 200,
              "the last scaled step sits on the target");

        std::printf("the loader ignored the extra keys in %zu compass sections:\n", mismatches.size());
        for (const std::string& line : mismatches) {
            std::printf("  %s\n", line.c_str());
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAIL threw: %s\n", error.what());
        ++failures;
    }

    if (failures != 0) {
        std::fprintf(stderr, "%d checks failed\n", failures);
        return 1;
    }
    std::printf("walk_test passed\n");
    return 0;
}
