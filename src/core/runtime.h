#pragma once
#include <filesystem>
#include <string>
namespace Runtime {
bool initialize(std::string &error);
const std::filesystem::path &photos();
const std::filesystem::path &settings();
void log(const std::string &message);
bool supportedExecutable();
} // namespace Runtime
