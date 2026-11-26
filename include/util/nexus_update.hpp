#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace inst::update {
inline constexpr char repo[] = "TR4UNEXUS/CyberFoil-Nexus";
inline constexpr char feed[] = "https://api.github.com/repos/TR4UNEXUS/CyberFoil-Nexus/releases/latest";
inline constexpr char assetName[] = "CyberFoil-Nexus.nro";
using Version = std::array<unsigned, 4>;  // Official version plus Nexus revision.
struct Release {
    std::string version, tag, notes, url, sha256;
    unsigned revision;
    uint64_t bytes;
};
std::optional<Release> parseFeed(const std::string& json, const std::string& installed, unsigned revision);
std::optional<Release> check();
void activatePending();
void setLaunchPath(const char* argv0);
void startBootCheck();
std::optional<Release> pollBootCheck();
void finishBootCheck();
void install(const Release& release, const std::function<bool(unsigned)>& progress);
}
