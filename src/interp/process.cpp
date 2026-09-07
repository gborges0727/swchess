#include "interp/process.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <spawn.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace swchess::interp {

RunResult runProgram(const std::vector<std::string>& argv) {
    if (argv.empty()) {
        throw std::runtime_error("cannot run an empty command");
    }

    char logPath[] = "/tmp/swchess-rife-XXXXXX";
    const int logFd = ::mkstemp(logPath);
    if (logFd < 0) {
        throw std::runtime_error("cannot make a file for the child's output");
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, logFd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, logFd, STDERR_FILENO);

    std::vector<char*> arguments;
    arguments.reserve(argv.size() + 1);
    for (const std::string& argument : argv) {
        arguments.push_back(const_cast<char*>(argument.c_str()));
    }
    arguments.push_back(nullptr);

    pid_t child = 0;
    const int started =
        ::posix_spawnp(&child, argv[0].c_str(), &actions, nullptr, arguments.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    if (started != 0) {
        ::close(logFd);
        ::unlink(logPath);
        throw std::runtime_error("cannot start " + argv[0] + ": " + std::strerror(started));
    }

    int status = 0;
    while (::waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            ::close(logFd);
            ::unlink(logPath);
            throw std::runtime_error("lost track of " + argv[0]);
        }
    }

    RunResult result;
    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.signalled = true;
        result.exitCode = 128 + WTERMSIG(status);
    }

    ::lseek(logFd, 0, SEEK_SET);
    char block[4096];
    ssize_t got = 0;
    while ((got = ::read(logFd, block, sizeof(block))) > 0) {
        result.output.append(block, static_cast<std::size_t>(got));
    }
    ::close(logFd);
    ::unlink(logPath);
    return result;
}

std::string joinArguments(const std::vector<std::string>& argv) {
    std::string out;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        if (i != 0) {
            out += ' ';
        }
        out += argv[i];
    }
    return out;
}

}  // namespace swchess::interp
