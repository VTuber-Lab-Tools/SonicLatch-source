#pragma once
#include <cstddef>
#include <cstdint>
namespace pitatuner::capture::format {
constexpr bool supportedRate(std::uint32_t r){return r==44100||r==48000;}
constexpr std::size_t windowFrames(std::uint32_t r){return supportedRate(r)?std::size_t(r)*2:0;}
// Call only for a supported, immutable session rate.
constexpr std::uint64_t frameNs(std::uint64_t frame,std::uint32_t r){return (frame/r)*1000000000ULL+(frame%r)*1000000000ULL/r;}
constexpr std::uint64_t nearestFrame(std::uint64_t ns,std::uint32_t r){return (ns/1000000000ULL)*r+((ns%1000000000ULL)*r+500000000ULL)/1000000000ULL;}
constexpr std::uint64_t sampleCeilNs(std::uint32_t r){return (1000000000ULL+r-1)/r;}
}
