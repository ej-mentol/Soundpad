#pragma once

#include <vector>
#include <string>
#include <cstdint>

namespace Sapi
{

struct SapiConfig
{
	float volume = 1.0f;       // 0.0 .. 1.0
	float rate = 0.0f;         // -10.0 .. 10.0 (Speech speed / rate)
	std::string voiceName;     // Optional voice name, empty = system default
};

// Check if SAPI 5 is available and can be instantiated on this system
bool IsAvailable();

// Synthesize text to 16-bit Mono PCM audio.
// Returns audio samples, and fills outSampleRate (22050 Hz).
std::vector<int16_t> SynthesizeText(const std::string& text, int& outSampleRate, const SapiConfig& cfg = {});

// Enumerate names of installed SAPI voices on the system
std::vector<std::string> GetAvailableVoices();

} // namespace Sapi
