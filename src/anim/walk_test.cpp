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
// the frame index of every picture it draws, in the order it draws them.
std::vector<std::size_t> sweep(const swchess::anim::WalkSequence& walk, double stepMs) {
    swchess::anim::WalkPlayer player;
    player.start(&walk, 0, 0, 240, 0, 0);
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

                // The frame the player shows depends on the clock and not on
                // how often the caller advances it.
                const std::vector<std::size_t> fine = sweep(walk, 1.0);
                const std::vector<std::size_t> coarse = sweep(walk, 1000.0 / 60.0);
                check(!fine.empty(), piece + " " + name + " draws at least one frame");
                check(fine == coarse,
                      piece + " " + name + " draws the same frames at both sweep rates");
                for (std::size_t f = 0; f + 1 < fine.size(); ++f) {
                    check(fine[f] + 1 == fine[f + 1],
                          piece + " " + name + " counts its frames up one at a time");
                }
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

        // The path is the straight screen line between the two square
        // centers. LineDDA reports one pixel per step along the longer axis
        // and LINEPROC keeps every other one.
        const std::vector<swchess::anim::PathPoint> straight =
            swchess::anim::linePoints(10, 10, 30, 14);
        check(straight.size() == 21, "a 20 by 4 line reports 21 points");
        check(straight.front() == (swchess::anim::PathPoint{10, 10}),
              "the line starts on the first point");
        check(straight.back() == (swchess::anim::PathPoint{30, 14}),
              "the line ends on the last point");
        const std::vector<swchess::anim::PathPoint> kept =
            swchess::anim::walkPath(10, 10, 30, 14);
        check(kept.size() == 11, "keeping every other point leaves 11 of the 21");
        check(kept[1] == straight[2], "the second kept point is the third of the line");
        check(kept.back() == straight.back(), "the last point is always kept");

        // The direction table of the research note, read off the board deltas.
        using swchess::anim::Direction;
        using swchess::anim::walkDirection;
        check(walkDirection(0, -1, false) == Direction::S, "moving down a rank walks south");
        check(walkDirection(0, 1, false) == Direction::N, "moving up a rank walks north");
        check(walkDirection(1, 0, false) == Direction::E, "moving up a file walks east");
        check(walkDirection(-1, 0, false) == Direction::W, "moving down a file walks west");
        check(walkDirection(-1, -1, false) == Direction::SW,
              "moving down and left walks southwest");
        check(walkDirection(1, 1, false) == Direction::NE, "moving up and right walks northeast");
        check(walkDirection(1, 2, false) == Direction::NE, "a knight walks by the signs alone");
        check(walkDirection(0, -1, true) == Direction::N, "a turned board reverses the direction");

        // The player, checked against AT walking south.
        swchess::anim::WalkSequence atSouth =
            swchess::anim::loadWalk(cdDir, "AT", swchess::anim::Direction::S);
        check(atSouth.steps.size() == 16, "AT [S] holds 16 walk frames");

        swchess::anim::WalkPlayer player;
        player.start(&atSouth, 100, 50, 100, 114, 1000);
        check(!player.isSliding(), "a sequence with frames walks rather than slides");
        check(player.path().size() == 33, "the 64 pixel walk keeps 33 of its 65 points");
        check(!player.isFinished(), "the player is running once it starts");

        swchess::anim::WalkUpdate first = player.advance(1000);
        check(first.draw.visible, "the first frame is on the screen at time zero");
        check(first.draw.stepIndex == 0, "the first frame is frame zero");
        check(first.draw.pointIndex == 0, "the first frame stands on the first point");
        check(first.draw.x == 100 && first.draw.y == 50,
              "the first frame stands on the square it left");
        check(first.draw.bitmap == atSouth.steps.front().bitmap,
              "the first frame draws the first bitmap of the sequence");

        // Every frame carries the piece the same number of points forward.
        const int pace = swchess::anim::kWalkPointsPerFrame;
        swchess::anim::WalkUpdate second = player.advance(1000 + 100);
        check(second.draw.stepIndex == 1, "the next frame replaces the first at 100 ms");
        check(second.draw.pointIndex == static_cast<std::size_t>(pace),
              "the second frame stands one frame's worth of points along the path");
        check(second.draw.y == player.path()[pace].y,
              "the second frame stands on that point of the path");

        // Time never runs backwards, so an earlier call leaves the clock alone.
        swchess::anim::WalkUpdate back = player.advance(1000);
        check(back.draw.stepIndex == 1, "an earlier time leaves the frame where it was");

        player.advance(1000 + player.durationMs());
        check(player.isFinished(), "the player finishes after the last frame");
        check(player.positions().back().pointIndex + 1 == player.path().size(),
              "the last frame stands on the last point of the path");
        check(player.positions().back().x == 100 && player.positions().back().y == 114,
              "the last frame stands on the square it walked to");
        check(player.durationMs() ==
                  static_cast<std::int64_t>(player.positions().size()) * 100,
              "the walk runs 100 ms per frame");

        // Every frame moves the piece forward and none of them overshoots.
        std::size_t previous = 0;
        for (const swchess::anim::WalkDraw& draw : player.positions()) {
            check(draw.pointIndex >= previous, "the walk never steps backwards");
            check(draw.pointIndex < player.path().size(), "the walk stays on its path");
            previous = draw.pointIndex;
        }

        // The enhanced cadence moves the piece by the clock instead of by the
        // 100 ms tick. It takes the same time, it never steps backwards, and
        // it stands on the tick's own point whenever the clock reaches a tick.
        swchess::anim::WalkPlayer smooth;
        smooth.setCadence(swchess::anim::Cadence::Interpolated60);
        smooth.start(&atSouth, 100, 50, 100, 114, 0);
        check(smooth.durationMs() == player.durationMs(),
              "the smooth walk takes as long as the stepped one");
        int previousY = 50;
        int movedFrames = 0;
        const std::int64_t stepMs = 1000 / 60;
        for (std::int64_t ms = 0; ms <= smooth.durationMs(); ms += stepMs) {
            const swchess::anim::WalkDraw draw = smooth.advance(ms).draw;
            check(draw.y >= previousY, "the smooth walk never moves backwards");
            check(draw.y <= 114, "the smooth walk never passes its target");
            if (draw.y != previousY) {
                ++movedFrames;
            }
            previousY = draw.y;
        }
        const swchess::anim::WalkDraw end = smooth.advance(smooth.durationMs()).draw;
        check(smooth.isFinished(), "the smooth walk finishes on its own duration");
        check(end.y == 114, "the smooth walk ends on the square it walked to");
        check(previousY >= 106, "the smooth walk is nearly there one frame before the end");
        // Nine steps of the stepped walk become more than forty moves here.
        check(movedFrames > 30, "the smooth walk moves on most display frames");

        swchess::anim::WalkPlayer onTicks;
        onTicks.setCadence(swchess::anim::Cadence::Interpolated60);
        onTicks.start(&atSouth, 100, 50, 100, 114, 0);
        for (std::size_t i = 0; i < onTicks.positions().size(); ++i) {
            const swchess::anim::WalkDraw draw =
                onTicks.advance(static_cast<std::int64_t>(i) * 100).draw;
            check(draw.x == onTicks.positions()[i].x && draw.y == onTicks.positions()[i].y,
                  "the smooth walk reaches each 100 ms point on time");
        }

        // A knight jumps two squares by one axis and one by the other, and it
        // walks the same straight line as everything else.
        swchess::anim::WalkSequence chewie =
            swchess::anim::loadWalk(cdDir, "CB", swchess::anim::Direction::NE);
        swchess::anim::WalkPlayer knight;
        knight.start(&chewie, 0, 0, 63, 126, 0);
        check(knight.path().front() == (swchess::anim::PathPoint{0, 0}),
              "the knight starts on its own square");
        check(knight.path().back() == (swchess::anim::PathPoint{63, 126}),
              "the knight ends on the square it jumps to");

        // With walking off the piece slides along the same line and draws no
        // frames, so the caller keeps drawing its ordinary sheet cell.
        swchess::anim::WalkPlayer slide;
        slide.start(nullptr, swchess::anim::walkPath(100, 50, 100, 114), 0);
        check(slide.isSliding(), "no sequence means the piece slides");
        check(slide.path().size() == 33, "the slide follows the same path as the walk");
        check(slide.advance(0).draw.bitmap == nullptr, "a sliding piece draws no walk frame");
        check(slide.advance(0).draw.visible, "a sliding piece is still on the screen");
        slide.advance(slide.durationMs());
        check(slide.isFinished(), "the slide finishes on its own duration");
        check(slide.positions().back().x == 100 && slide.positions().back().y == 114,
              "the slide ends on the target square");

        // The rotation set is the one place the INI dx and dy still move a
        // piece. It turns in place around the square it stands on.
        swchess::anim::WalkSequence rotation = swchess::anim::loadRotation(cdDir, "AT");
        const std::vector<swchess::anim::TurnPose> turn =
            swchess::anim::turnInPlace(rotation, 300, 200);
        check(turn.size() == rotation.steps.size(), "the turn holds one pose per rotation frame");
        check(turn.front().x == 300 + rotation.steps.front().dx &&
                  turn.front().y == 200 + rotation.steps.front().dy,
              "a turning pose sits at the base plus the INI offsets");

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
