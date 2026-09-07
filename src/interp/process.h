// Runs a child program from an argument list, with no shell anywhere.
//
// The interpolation helper starts rife-ncnn-vulkan once per transition per
// channel. Every path it passes comes from the player's own directories, so
// none of it may ever reach a shell for word splitting. posix_spawn takes the
// argument array straight through to execve.
//
// posix_spawnp searches PATH only when the program name holds no slash, so a
// path the caller gives is used exactly as it stands.
//
// The child's output goes to a temporary file rather than a pipe, so several
// runs at once cannot deadlock on a full pipe buffer.
#pragma once

#include <string>
#include <vector>

namespace swchess::interp {

// What one child left behind.
struct RunResult {
    int exitCode = -1;
    bool signalled = false;   // the child died on a signal instead of exiting
    std::string output;       // whatever it wrote to stdout and stderr
};

// Starts `argv[0]` with `argv` as its arguments and waits for it. Throws
// std::runtime_error when the program cannot be started at all.
RunResult runProgram(const std::vector<std::string>& argv);

// Joins an argument list with single spaces, the way tools/interp writes the
// example command into its manifest.
std::string joinArguments(const std::vector<std::string>& argv);

}  // namespace swchess::interp
