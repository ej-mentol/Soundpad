#pragma once

// DSP (fade/reverb/zigzag) only runs on WAV because it decodes PCM itself.
// MP3/OGG are not decoded here, so the effects are WAV-only and the dialogs say so.
#define SOUNDPAD_ENABLE_DSP 0

// Slicer UI is hidden until slicing works for every format; macro steps play whole files.
#define SOUNDPAD_ENABLE_SLICER 0

#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>


enum class FadeCurveType
{
	Linear = 0,
	Quadratic = 1,
	Sine = 2
};

struct SoundPlayParams
{
	float volume = 1.0f;
	float pitch = 1.0f;
	float startSec = 0.0f;     // Slice in (0 = from start)
	float endSec = 0.0f;       // Slice out (0 = to end of sound)
	float fadeInSec = 0.0f;    // Fade in duration in seconds
	float fadeOutSec = 0.0f;   // Fade out duration in seconds
	FadeCurveType fadeCurve = FadeCurveType::Sine;
	bool enqueue = false;
	bool fxReverb = false;     // Bunker/tunnel comb-delay echo
	bool fxZigzag = false;     // Amplitude LFO tremolo (chat sound style)
	bool localPreviewOnly = false; // Preview locally without voice datagram broadcast
	bool priorityCutIn = false;    // Priority interleave: pause background, play now, resume background afterwards
	bool isTTS = false;            // Flag indicating speech synthesis stream
};

struct PlaybackStatus
{
	bool isPlaying = false;
	bool isPaused = false;
	bool isDucking = false;
	std::string currentSound;
	float volume = 1.0f;
	float pitch = 1.0f;
	size_t queueSize = 0;
	size_t chunksRemaining = 0;
	double secondsRemaining = 0.0;
};

void SoundEngine_Init();
void SoundEngine_Shutdown();

// Plays a sound. If enqueue is false, cuts in immediately (Soundpad cut-and-restart).
// If enqueue is true, appends to the queue after the current playback.
bool SoundEngine_Play(const char* relativePath, float volume = 1.0f, float pitch = 1.0f, bool enqueue = false);

// Advanced play with timeline slicing, fade envelopes and DSP effects
bool SoundEngine_PlayEx(const char* relativePath, const SoundPlayParams& params);

// Plays in-memory PCM mono audio buffer directly into voice datagram and/or local preview
bool SoundEngine_PlayPCM(const int16_t* pcm, size_t numSamples, int sampleRate, const SoundPlayParams& params, const char* identifier = "In-Memory PCM");

// Synthesizes speech with Klatter formant engine and plays into voice datagram or local preview
bool SoundEngine_SpeakTTS(const char* text, float volume = 1.0f, float pitch = 1.0f, bool repoRandomPitch = true, bool whisper = false, bool localOnly = false, bool priorityInterleave = true, bool randomStyle = false, bool enqueue = true);

// Master Volume & Mute control
void SoundEngine_SetMasterVolume(float vol);
float SoundEngine_GetMasterVolume();
void SoundEngine_SetMuted(bool mute);
bool SoundEngine_IsMuted();

// Inspect audio wave duration in seconds
float SoundEngine_GetWaveDuration(const char* relativePath);

// Stops active playback immediately and flushes the queue.
void SoundEngine_Stop();

// Stops a specific track by its ID (if supported by VoiceExtension3)
bool SoundEngine_StopTrack(uint32_t trackId);

// Previews a sound locally through the client sound engine (no voice datagram broadcast).
void SoundEngine_PreviewLocal(const char* relativePath, float volume = 1.0f, float pitch = 1.0f, bool isMonitor = false);

// Helper for local preview/monitor playback
void SoundEngine_PreviewSound(const char* relativePath, float volume = 1.0f, float pitch = 1.0f, bool isMonitor = false);

// Check if the local player is in a live map. This is only a client-side
// heuristic; use IVoiceExtension2::IsReady() before enqueueing audio.
bool SoundEngine_IsConnected();

// Called from the DLL unload notification. Drops all VoiceExtension-owned state
// without calling into the module that is being unloaded.
void SoundEngine_OnVoiceExtensionUnloaded();

// True while Soundpad is registered as a listener (or client) of VoiceExtension.
bool SoundEngine_IsVoiceBound();

// Called from HUD_Frame for diagnostic frame counting and telemetry.
void SoundEngine_OnHudFrame(double time);

// Query playback status for UI display.
PlaybackStatus SoundEngine_GetStatus();

// Print detailed diagnostic telemetry to console and clipboard.
void SoundEngine_DumpReport();
