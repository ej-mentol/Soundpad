#include "SoundEngine.h"
#include "SoundpadConfig.h"
#include "plugins.h"
#include "KlatterEngine.h"
#include "SapiEngine.h"
#include "GameState.h"
#include "VoiceExtensionImport.h"
#include "DebugLog.h"

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include <windows.h>
#include <mmsystem.h>
#include <strsafe.h>
#pragma comment(lib, "winmm.lib")
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <cstring>

namespace
{
	constexpr double kPi = 3.14159265358979323846;

	float g_masterVolume = 1.0f;
	bool g_isMuted = false;

	std::mutex s_statusMutex;
	std::string s_activeTrackName;

	struct MonitorAudio
	{
		std::vector<int16_t> pcm;
		std::string filePath;
		int sampleRate = 0;
		float volume = 1.0f;
		float pitch = 1.0f;
	};

	static std::mutex s_monitorMutex;
	static std::unordered_map<uint32_t, MonitorAudio> s_monitorTracks;
	static std::unordered_map<uint32_t, uint32_t> s_coreToCallerTrackId;
	static std::atomic<uint32_t> s_nextCallerTrackId{ 1000 };
	static uint32_t s_soundpadClientId = 0;
	static bool s_listenerRegistered = false;

	void PlayWavFromMemory(const std::vector<int16_t>& pcm, int sampleRate, float volume, float pitch);

	class CSoundpadVoiceListener : public IVoiceEventListener
	{
	public:
		void OnTrackStarted(uint32_t trackId, const char* name) override
		{
			SP_DEBUG_LOG("[Soundpad] OnTrackStarted: trackId=%u, name='%s'\n", trackId, name ? name : "");
			{
				std::lock_guard<std::mutex> lock(s_statusMutex);
				s_activeTrackName = name ? name : "";
			}

			MonitorAudio mon;
			bool hasMonitor = false;
			{
				std::lock_guard<std::mutex> lock(s_monitorMutex);
				auto it = s_monitorTracks.find(trackId);
				if (it == s_monitorTracks.end())
				{
					auto cit = s_coreToCallerTrackId.find(trackId);
					if (cit != s_coreToCallerTrackId.end())
						it = s_monitorTracks.find(cit->second);
				}

				if (it != s_monitorTracks.end())
				{
					mon = std::move(it->second);
					s_monitorTracks.erase(it);
					hasMonitor = true;
				}
			}

			if (hasMonitor)
			{
				// Offload local monitor playback to a background thread to prevent stalling Voice_Send / game frame
				std::thread([monAudio = std::move(mon)]() {
					if (!monAudio.pcm.empty() && monAudio.sampleRate > 0)
					{
						PlayWavFromMemory(monAudio.pcm, monAudio.sampleRate, monAudio.volume, monAudio.pitch);
					}
					else if (!monAudio.filePath.empty())
					{
						SoundEngine_PreviewLocal(monAudio.filePath.c_str(), monAudio.volume, monAudio.pitch, true);
					}
				}).detach();
			}
		}

		void OnTrackFinished(uint32_t trackId, const char* name) override
		{
			SP_DEBUG_LOG("[Soundpad] OnTrackFinished: trackId=%u, name='%s'\n", trackId, name ? name : "");
			{
				std::lock_guard<std::mutex> lock(s_statusMutex);
				if (name && s_activeTrackName == name)
					s_activeTrackName.clear();
				else if (!name)
					s_activeTrackName.clear();
			}
			{
				std::lock_guard<std::mutex> lock(s_monitorMutex);
				s_monitorTracks.erase(trackId);
				auto it = s_coreToCallerTrackId.find(trackId);
				if (it != s_coreToCallerTrackId.end())
				{
					s_monitorTracks.erase(it->second);
					s_coreToCallerTrackId.erase(it);
				}
				for (auto cit = s_coreToCallerTrackId.begin(); cit != s_coreToCallerTrackId.end();)
				{
					if (cit->second == trackId)
						cit = s_coreToCallerTrackId.erase(cit);
					else
						++cit;
				}
			}
		}

		void OnStateChanged(VoicePlaybackState newState) override
		{
			SP_DEBUG_LOG("[Soundpad] OnStateChanged: newState=%d\n", static_cast<int>(newState));
			std::lock_guard<std::mutex> lock(s_statusMutex);
			if (newState == VoicePlaybackState::IDLE)
				s_activeTrackName.clear();
		}
	};

	static CSoundpadVoiceListener s_voiceListener;

	void ApplyDspEffects(std::vector<float>& samples, int sampleRate, const SoundPlayParams& params)
	{
		if (samples.empty() || sampleRate <= 0)
			return;

		// Slicing (startSec / endSec)
		if (params.startSec > 0.0f || params.endSec > 0.0f)
		{
			size_t startSample = static_cast<size_t>(params.startSec * static_cast<float>(sampleRate));
			size_t endSample = (params.endSec > params.startSec)
				? static_cast<size_t>(params.endSec * static_cast<float>(sampleRate))
				: samples.size();

			if (startSample >= samples.size())
			{
				samples.clear();
				return;
			}
			if (endSample > samples.size()) endSample = samples.size();
			if (endSample > startSample)
			{
				samples = std::vector<float>(samples.begin() + startSample, samples.begin() + endSample);
			}
		}

#if SOUNDPAD_ENABLE_DSP
		// Reverb (comb filter delay)
		if (params.fxReverb && samples.size() > static_cast<size_t>(sampleRate * 85 / 1000))
		{
			size_t reverbDelay = static_cast<size_t>(sampleRate * 85 / 1000);
			constexpr float kReverbDecay = 0.38f;
			for (size_t i = reverbDelay; i < samples.size(); ++i)
			{
				samples[i] += samples[i - reverbDelay] * kReverbDecay;
			}
		}

		// Zigzag (tremolo amplitude modulation)
		if (params.fxZigzag && !samples.empty())
		{
			constexpr double kLfoFreq = 8.0;
			for (size_t i = 0; i < samples.size(); ++i)
			{
				double tSec = static_cast<double>(i) / static_cast<double>(sampleRate);
				double phase = std::fmod(tSec * kLfoFreq, 1.0);
				double tri = (phase < 0.5) ? (phase * 2.0) : (2.0 - phase * 2.0);
				double mod = 0.5 + 0.5 * tri;
				samples[i] = static_cast<float>(static_cast<double>(samples[i]) * mod);
			}
		}

		// Fade In
		if (params.fadeInSec > 0.0f && !samples.empty())
		{
			size_t inSamples = static_cast<size_t>(params.fadeInSec * static_cast<float>(sampleRate));
			if (inSamples > samples.size()) inSamples = samples.size();
			for (size_t i = 0; i < inSamples; ++i)
			{
				double t = static_cast<double>(i) / static_cast<double>(inSamples);
				double k = t;
				if (params.fadeCurve == FadeCurveType::Quadratic)
					k = t * t;
				else if (params.fadeCurve == FadeCurveType::Sine)
					k = std::sin(kPi * 0.5 * t);

				samples[i] *= static_cast<float>(k);
			}
		}

		// Fade Out
		if (params.fadeOutSec > 0.0f && !samples.empty())
		{
			size_t outSamples = static_cast<size_t>(params.fadeOutSec * static_cast<float>(sampleRate));
			if (outSamples > samples.size()) outSamples = samples.size();
			size_t offset = samples.size() - outSamples;
			for (size_t i = 0; i < outSamples; ++i)
			{
				double t = static_cast<double>(i) / static_cast<double>(outSamples);
				double k = 1.0 - t;
				if (params.fadeCurve == FadeCurveType::Quadratic)
					k = (1.0 - t) * (1.0 - t);
				else if (params.fadeCurve == FadeCurveType::Sine)
					k = std::cos(kPi * 0.5 * t);

				samples[offset + i] *= static_cast<float>(k);
			}
		}
#endif
	}

	void FloatToPcm16(const std::vector<float>& src, std::vector<int16_t>& dst)
	{
		dst.resize(src.size());
		for (size_t i = 0; i < src.size(); ++i)
		{
			float v = src[i] * 32767.0f;
			if (v > 32767.0f) v = 32767.0f;
			else if (v < -32768.0f) v = -32768.0f;
			dst[i] = static_cast<int16_t>(v);
		}
	}

	VoicePlayParams ConvertParams(const SoundPlayParams& sp)
	{
		VoicePlayParams vp;
		vp.volume = sp.volume * g_masterVolume;
		vp.pitch = sp.pitch;
		vp.priority = sp.priorityCutIn ? VoicePriority::HIGH : VoicePriority::NORMAL;

		// VE pauses transmission while the physical mic is held. The two cvars
		// are kept as aliases; sp_duck_level has no API_003 equivalent because
		// DUCKED pauses the track instead of attenuating its volume.
		if (sp_mic_duck || sp_pause_on_mic)
		{
			vp.allowDucking =
				(sp_mic_duck && sp_mic_duck->value > 0.0f) ||
				(sp_pause_on_mic && sp_pause_on_mic->value > 0.0f);
		}

		return vp;
	}

	bool WritePcmToWavFile(const char* fullPath, const int16_t* pcm, size_t numSamples, int sampleRate)
	{
		if (!fullPath || !pcm || numSamples == 0 || sampleRate <= 0)
			return false;

		std::error_code ec;
		std::filesystem::path p(fullPath);
		std::filesystem::create_directories(p.parent_path(), ec);

		std::ofstream f(fullPath, std::ios::binary);
		if (!f.is_open())
			return false;

		uint32_t dataSize = static_cast<uint32_t>(numSamples * sizeof(int16_t));
		uint32_t chunkSize = 36 + dataSize;
		uint32_t byteRate = static_cast<uint32_t>(sampleRate * 2);
		uint16_t blockAlign = 2;
		uint16_t numChannels = 1;
		uint16_t bitsPerSample = 16;
		uint16_t audioFormat = 1;
		uint32_t subchunk1Size = 16;

		f.write("RIFF", 4);
		f.write(reinterpret_cast<const char*>(&chunkSize), 4);
		f.write("WAVEfmt ", 8);
		f.write(reinterpret_cast<const char*>(&subchunk1Size), 4);
		f.write(reinterpret_cast<const char*>(&audioFormat), 2);
		f.write(reinterpret_cast<const char*>(&numChannels), 2);
		f.write(reinterpret_cast<const char*>(&sampleRate), 4);
		f.write(reinterpret_cast<const char*>(&byteRate), 4);
		f.write(reinterpret_cast<const char*>(&blockAlign), 2);
		f.write(reinterpret_cast<const char*>(&bitsPerSample), 2);
		f.write("data", 4);
		f.write(reinterpret_cast<const char*>(&dataSize), 4);
		f.write(reinterpret_cast<const char*>(pcm), dataSize);

		return true;
	}

	std::vector<int16_t> ResampleLinear(const std::vector<int16_t>& in, int inRate, int outRate)
	{
		if (inRate == outRate || in.empty() || inRate <= 0 || outRate <= 0)
			return in;

		size_t outSamples = static_cast<size_t>(static_cast<double>(in.size()) * outRate / inRate);
		if (outSamples == 0)
			return in;

		std::vector<int16_t> out(outSamples);
		double ratio = static_cast<double>(inRate) / static_cast<double>(outRate);

		for (size_t i = 0; i < outSamples; ++i)
		{
			double srcPos = i * ratio;
			size_t idx = static_cast<size_t>(srcPos);
			double frac = srcPos - idx;

			if (idx + 1 < in.size())
			{
				double s0 = in[idx];
				double s1 = in[idx + 1];
				out[i] = static_cast<int16_t>(s0 + frac * (s1 - s0));
			}
			else if (idx < in.size())
			{
				out[i] = in[idx];
			}
			else
			{
				out[i] = 0;
			}
		}
		return out;
	}

	std::string ResolveSoundFilePath(const char* relativePath)
	{
		if (!relativePath || !*relativePath)
			return "";

		// 0. Check if relativePath is already an existing path on disk (e.g. absolute path or cwd relative)
		std::error_code ec;
		if (std::filesystem::exists(relativePath, ec) && std::filesystem::is_regular_file(relativePath, ec))
			return relativePath;

		std::string sub = relativePath;
		for (char& c : sub)
		{
			if (c == '\\')
				c = '/';
		}

		if (sub.rfind("sound/", 0) == 0)
		{
			sub = sub.substr(6);
		}

		// 1. Try engine's IFileSystem
		if (g_pFileSystem)
		{
			std::string fsRelative = "sound/" + sub;
			char localPath[MAX_PATH] = {};
			if (g_pFileSystem->GetLocalPath(fsRelative.c_str(), localPath, sizeof(localPath)) && localPath[0] &&
			    std::filesystem::exists(localPath, ec) && std::filesystem::is_regular_file(localPath, ec))
			{
				return localPath;
			}

			static const char* kExts[] = { ".wav", ".mp3", ".ogg" };
			for (const char* ext : kExts)
			{
				std::string withExt = fsRelative + ext;
				if (g_pFileSystem->GetLocalPath(withExt.c_str(), localPath, sizeof(localPath)) && localPath[0] &&
				    std::filesystem::exists(localPath, ec) && std::filesystem::is_regular_file(localPath, ec))
				{
					return localPath;
				}
			}
		}

		// 2. Direct filesystem lookup across standard GoldSrc/Sven Co-op directories
		const char* gamedir = (gEngfuncs.pfnGetGameDirectory && gEngfuncs.pfnGetGameDirectory())
			? gEngfuncs.pfnGetGameDirectory() : "svencoop";

		const char* searchDirs[] = {
			gamedir,
			"svencoop_addon",
			"svencoop_downloads",
			"svencoop",
			"valve"
		};

		for (const char* dir : searchDirs)
		{
			std::string candidate = std::string(dir) + "/sound/" + sub;
			if (std::filesystem::exists(candidate, ec) && std::filesystem::is_regular_file(candidate, ec))
				return candidate;

			static const char* kExts[] = { ".wav", ".mp3", ".ogg" };
			for (const char* ext : kExts)
			{
				std::string withExt = candidate + ext;
				if (std::filesystem::exists(withExt, ec) && std::filesystem::is_regular_file(withExt, ec))
					return withExt;
			}
		}

		return "";
	}

	bool ReadSoundFileBytes(const char* path, std::vector<uint8_t>& outBytes)
	{
		if (!path || !*path)
			return false;

		std::error_code ec;

		constexpr std::streamsize kMaxSoundFileSize = 128 * 1024 * 1024; // 128 MB sanity limit

		// 1. Check direct path on disk
		if (std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec))
		{
			std::ifstream f(path, std::ios::binary | std::ios::ate);
			if (f.is_open())
			{
				std::streamsize sz = f.tellg();
				f.seekg(0, std::ios::beg);
				if (sz > 0 && sz <= kMaxSoundFileSize)
				{
					outBytes.resize(static_cast<size_t>(sz));
					if (f.read(reinterpret_cast<char*>(outBytes.data()), sz))
						return true;
				}
			}
		}

		// 2. Check resolved path on disk
		std::string resolved = ResolveSoundFilePath(path);
		if (!resolved.empty() && std::filesystem::exists(resolved, ec) && std::filesystem::is_regular_file(resolved, ec))
		{
			std::ifstream f(resolved, std::ios::binary | std::ios::ate);
			if (f.is_open())
			{
				std::streamsize sz = f.tellg();
				f.seekg(0, std::ios::beg);
				if (sz > 0 && sz <= kMaxSoundFileSize)
				{
					outBytes.resize(static_cast<size_t>(sz));
					if (f.read(reinterpret_cast<char*>(outBytes.data()), sz))
						return true;
				}
			}
		}

		// 3. IFileSystem lookup (covers loose files or mounted archives)
		if (g_pFileSystem)
		{
			std::string sub = path;
			for (char& c : sub)
			{
				if (c == '\\')
					c = '/';
			}
			if (sub.rfind("sound/", 0) == 0)
				sub = sub.substr(6);

			std::string fsRelative = "sound/" + sub;
			FileHandle_t fh = g_pFileSystem->Open(fsRelative.c_str(), "rb");
			if (!fh)
			{
				static const char* kExts[] = { ".wav", ".mp3", ".ogg" };
				for (const char* ext : kExts)
				{
					std::string withExt = fsRelative + ext;
					fh = g_pFileSystem->Open(withExt.c_str(), "rb");
					if (fh)
						break;
				}
			}

			if (fh)
			{
				unsigned int sz = g_pFileSystem->Size(fh);
				if (sz > 0 && sz <= static_cast<unsigned int>(kMaxSoundFileSize))
				{
					outBytes.resize(sz);
					int bytesRead = g_pFileSystem->Read(outBytes.data(), sz, fh);
					g_pFileSystem->Close(fh);
					if (bytesRead > 0)
					{
						outBytes.resize(bytesRead);
						return true;
					}
				}
				else
				{
					g_pFileSystem->Close(fh);
				}
			}
		}

		return false;
	}

	bool ReadMp3FromBytes(const uint8_t* data, size_t size, std::vector<int16_t>& outPcm, int& outSampleRate)
	{
		if (!data || size < 16)
			return false;

		mp3dec_t mp3d;
		mp3dec_init(&mp3d);
		mp3dec_frame_info_t info = {};
		int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

		size_t offset = 0;
		int sampleRate = 0;
		int channels = 0;
		outPcm.clear();

		constexpr size_t kMaxSamples = 48000 * 600;

		while (offset < size)
		{
			int samples = mp3dec_decode_frame(&mp3d, data + offset, static_cast<int>(size - offset), pcm, &info);
			offset += info.frame_bytes;

			if (samples > 0)
			{
				if (sampleRate == 0)
				{
					sampleRate = info.hz;
					channels = info.channels;
					if (sampleRate <= 0 || sampleRate > 192000 || channels <= 0 || channels > 2)
						return false;
				}
				else if (info.hz != sampleRate || info.channels != channels)
				{
					break;
				}

				if (channels == 1)
				{
					outPcm.insert(outPcm.end(), pcm, pcm + samples);
				}
				else if (channels == 2)
				{
					for (int i = 0; i < samples; ++i)
					{
						int32_t mixed = (static_cast<int32_t>(pcm[i * 2]) + static_cast<int32_t>(pcm[i * 2 + 1])) / 2;
						outPcm.push_back(static_cast<int16_t>(mixed));
					}
				}

				if (outPcm.size() > kMaxSamples)
					break;
			}

			if (info.frame_bytes == 0)
				break;
		}

		if (outPcm.empty() || sampleRate <= 0)
			return false;

		outSampleRate = sampleRate;
		return true;
	}

	bool ReadPcmWavFromBytes(const uint8_t* data, size_t size, std::vector<int16_t>& outPcm, int& outSampleRate)
	{
		if (!data || size < 16)
			return false;

		if (size < 44 || std::memcmp(data, "RIFF", 4) != 0 || std::memcmp(data + 8, "WAVE", 4) != 0)
			return ReadMp3FromBytes(data, size, outPcm, outSampleRate);

		uint16_t formatTag = 0;
		uint16_t numChannels = 0;
		uint32_t sampleRate = 0;
		uint16_t bitsPerSample = 0;
		const uint8_t* pcmData = nullptr;
		size_t pcmDataBytes = 0;

		size_t offset = 12;
		while (offset + 8 <= size)
		{
			char chunkId[4];
			std::memcpy(chunkId, data + offset, 4);
			uint32_t chunkSize = 0;
			std::memcpy(&chunkSize, data + offset + 4, 4);
			offset += 8;

			if (chunkSize > size - offset)
				chunkSize = static_cast<uint32_t>(size - offset);

			if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16)
			{
				std::memcpy(&formatTag, data + offset, 2);
				std::memcpy(&numChannels, data + offset + 2, 2);
				std::memcpy(&sampleRate, data + offset + 4, 4);
				std::memcpy(&bitsPerSample, data + offset + 14, 2);

				// Support WAVE_FORMAT_EXTENSIBLE (0xFFFE)
				if (formatTag == 0xFFFE && chunkSize >= 40)
				{
					uint16_t subFormat = 0;
					std::memcpy(&subFormat, data + offset + 24, 2);
					if (subFormat == 1 || subFormat == 3)
						formatTag = subFormat;
				}
			}
			else if (std::memcmp(chunkId, "data", 4) == 0)
			{
				pcmData = data + offset;
				pcmDataBytes = chunkSize;
				break;
			}

			// RIFF chunks are padded to even 2-byte boundaries
			offset += (chunkSize + 1) & ~1;
		}

		if (!pcmData || pcmDataBytes == 0 || sampleRate == 0 || numChannels == 0)
			return false;

		if (formatTag != 1 && formatTag != 3) // 1 = PCM, 3 = IEEE Float
			return false;

		size_t bytesPerSample = bitsPerSample / 8;
		if (bytesPerSample == 0)
			return false;

		size_t totalRawSamples = pcmDataBytes / bytesPerSample;
		size_t totalFrames = totalRawSamples / numChannels;
		if (totalFrames == 0)
			return false;

		outPcm.resize(totalFrames);

		if (formatTag == 1) // Standard PCM
		{
			if (bitsPerSample == 16)
			{
				for (size_t f = 0; f < totalFrames; ++f)
				{
					int32_t sum = 0;
					for (uint16_t ch = 0; ch < numChannels; ++ch)
					{
						int16_t sample = 0;
						std::memcpy(&sample, pcmData + (f * numChannels + ch) * 2, sizeof(sample));
						sum += sample;
					}
					outPcm[f] = static_cast<int16_t>(sum / numChannels);
				}
			}
			else if (bitsPerSample == 8)
			{
				for (size_t f = 0; f < totalFrames; ++f)
				{
					int32_t sum = 0;
					for (uint16_t ch = 0; ch < numChannels; ++ch)
					{
						int s16 = (static_cast<int>(pcmData[f * numChannels + ch]) - 128) << 8;
						sum += s16;
					}
					outPcm[f] = static_cast<int16_t>(sum / numChannels);
				}
			}
			else if (bitsPerSample == 24)
			{
				for (size_t f = 0; f < totalFrames; ++f)
				{
					int32_t sum = 0;
					for (uint16_t ch = 0; ch < numChannels; ++ch)
					{
						size_t sIdx = (f * numChannels + ch) * 3;
						int32_t v = (pcmData[sIdx] << 8) | (pcmData[sIdx + 1] << 16) | (pcmData[sIdx + 2] << 24);
						sum += (v >> 16);
					}
					outPcm[f] = static_cast<int16_t>(sum / numChannels);
				}
			}
			else if (bitsPerSample == 32)
			{
				for (size_t f = 0; f < totalFrames; ++f)
				{
					int64_t sum = 0;
					for (uint16_t ch = 0; ch < numChannels; ++ch)
					{
						int32_t sample = 0;
						std::memcpy(&sample, pcmData + (f * numChannels + ch) * 4, sizeof(sample));
						sum += (sample >> 16);
					}
					outPcm[f] = static_cast<int16_t>(sum / numChannels);
				}
			}
			else
			{
				return false;
			}
		}
		else if (formatTag == 3 && bitsPerSample == 32) // 32-bit IEEE Float
		{
			for (size_t f = 0; f < totalFrames; ++f)
			{
				float sum = 0.0f;
				for (uint16_t ch = 0; ch < numChannels; ++ch)
				{
					float sample = 0.0f;
					std::memcpy(&sample, pcmData + (f * numChannels + ch) * 4, sizeof(sample));
					sum += sample;
				}
				float avg = sum / static_cast<float>(numChannels);
				if (avg > 1.0f) avg = 1.0f;
				if (avg < -1.0f) avg = -1.0f;
				outPcm[f] = static_cast<int16_t>(avg * 32767.0f);
			}
		}
		else
		{
			return false;
		}

		outSampleRate = static_cast<int>(sampleRate);
		return true;
	}

	float ReadWavDuration(const char* path)
	{
		std::vector<uint8_t> bytes;
		if (!ReadSoundFileBytes(path, bytes) || bytes.size() < 44)
			return 0.0f;

		if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
			return 0.0f;

		uint32_t sampleRate = 0;
		uint16_t numChannels = 0;
		uint16_t bitsPerSample = 0;
		uint32_t dataBytes = 0;

		size_t offset = 12;
		while (offset + 8 <= bytes.size())
		{
			char chunkId[4];
			std::memcpy(chunkId, bytes.data() + offset, 4);
			uint32_t chunkSize = 0;
			std::memcpy(&chunkSize, bytes.data() + offset + 4, 4);
			offset += 8;

			if (chunkSize > bytes.size() - offset)
				chunkSize = static_cast<uint32_t>(bytes.size() - offset);

			if (std::memcmp(chunkId, "fmt ", 4) == 0 && chunkSize >= 16)
			{
				std::memcpy(&numChannels, bytes.data() + offset + 2, 2);
				std::memcpy(&sampleRate, bytes.data() + offset + 4, 4);
				std::memcpy(&bitsPerSample, bytes.data() + offset + 14, 2);
			}
			else if (std::memcmp(chunkId, "data", 4) == 0)
			{
				dataBytes = chunkSize;
				break;
			}

			offset += (chunkSize + 1) & ~1;
		}

		if (sampleRate > 0 && numChannels > 0 && bitsPerSample > 0 && dataBytes > 0)
		{
			uint32_t bytesPerSec = sampleRate * numChannels * (bitsPerSample / 8);
			if (bytesPerSec > 0)
				return static_cast<float>(dataBytes) / static_cast<float>(bytesPerSec);
		}

		return 0.0f;
	}

	bool ReadPcmWav(const char* path, std::vector<int16_t>& outPcm, int& outSampleRate)
	{
		std::vector<uint8_t> bytes;
		if (!ReadSoundFileBytes(path, bytes))
			return false;

		return ReadPcmWavFromBytes(bytes.data(), bytes.size(), outPcm, outSampleRate);
	}

	// Local preview/monitor for MP3 memes. GoldSrc's pfnPlaySoundByNameAtPitch is
	// WAV-only, so without this fallback an MP3 chat trigger is silent locally even
	// though VoiceExtension transmits it.
	bool ReadMp3Pcm(const char* path, std::vector<int16_t>& outPcm, int& outSampleRate)
	{
		try
		{
			std::vector<uint8_t> bytes;
			if (!ReadSoundFileBytes(path, bytes) || bytes.empty())
				return false;

			mp3dec_t decoder;
			mp3dec_init(&decoder);

			mp3dec_frame_info_t info = {};
			int16_t frame[MINIMP3_MAX_SAMPLES_PER_FRAME];
			std::vector<int16_t> mono;
			int sampleRate = 0;
			int channels = 0;
			size_t offset = 0;

			while (offset < bytes.size())
			{
				const int samples = mp3dec_decode_frame(&decoder, bytes.data() + offset,
					static_cast<int>(bytes.size() - offset), frame, &info);
				if (info.frame_bytes == 0)
					break;
				offset += info.frame_bytes;

				if (samples <= 0)
					continue;

				if (sampleRate == 0)
				{
					sampleRate = info.hz;
					channels = info.channels;
					if (sampleRate <= 0 || sampleRate > 192000 || channels <= 0 || channels > 2)
						return false;
				}
				else if (info.hz != sampleRate || info.channels != channels)
				{
					break;
				}

				if (channels == 1)
				{
					mono.insert(mono.end(), frame, frame + samples);
				}
				else
				{
					for (int i = 0; i < samples; ++i)
					{
						const int32_t mixed = static_cast<int32_t>(frame[i * 2]) + frame[i * 2 + 1];
						mono.push_back(static_cast<int16_t>(mixed / 2));
					}
				}

				if (sampleRate > 0 && mono.size() > static_cast<size_t>(sampleRate) * 600)
					break;
			}

			if (mono.empty() || sampleRate <= 0)
				return false;

			outPcm = std::move(mono);
			outSampleRate = sampleRate;
			return true;
		}
		catch (...)
		{
			return false;
		}
	}

	static std::vector<uint8_t> s_currentPreviewMemory;
	static std::mutex s_previewMutex;

	void PlayWavFromMemory(const std::vector<int16_t>& pcm, int sampleRate, float volume, float pitch)
	{
		std::lock_guard<std::mutex> lock(s_previewMutex);
		::PlaySoundA(NULL, NULL, 0);

		if (pcm.empty() || sampleRate <= 0)
			return;

		float vol = (std::max)(0.01f, (std::min)(1.0f, volume * g_masterVolume));
		std::vector<int16_t> scaledPcm = pcm;
		if (vol < 0.99f || vol > 1.01f)
		{
			for (auto& s : scaledPcm)
			{
				int32_t val = static_cast<int32_t>(s * vol);
				s = static_cast<int16_t>(std::clamp(val, -32768, 32767));
			}
		}

		float p = std::clamp(pitch, 0.25f, 2.5f);
		uint32_t effRate = static_cast<uint32_t>(static_cast<float>(sampleRate) * p);
		if (effRate < 4000) effRate = 4000;
		if (effRate > 192000) effRate = 192000;

		uint32_t pcmBytes = static_cast<uint32_t>(scaledPcm.size() * sizeof(int16_t));
		uint32_t totalSize = 44 + pcmBytes;
		s_currentPreviewMemory.resize(totalSize);
		uint8_t* out = s_currentPreviewMemory.data();

		// RIFF header
		std::memcpy(out + 0, "RIFF", 4);
		uint32_t riffSize = totalSize - 8;
		std::memcpy(out + 4, &riffSize, 4);
		std::memcpy(out + 8, "WAVE", 4);

		// fmt chunk
		std::memcpy(out + 12, "fmt ", 4);
		uint32_t fmtSize = 16;
		std::memcpy(out + 16, &fmtSize, 4);
		uint16_t formatTag = 1;
		std::memcpy(out + 20, &formatTag, 2);
		uint16_t channels = 1;
		std::memcpy(out + 22, &channels, 2);
		std::memcpy(out + 24, &effRate, 4);
		uint32_t byteRate = effRate * channels * sizeof(int16_t);
		std::memcpy(out + 28, &byteRate, 4);
		uint16_t blockAlign = channels * sizeof(int16_t);
		std::memcpy(out + 32, &blockAlign, 2);
		uint16_t bits = 16;
		std::memcpy(out + 34, &bits, 2);

		// data chunk
		std::memcpy(out + 36, "data", 4);
		std::memcpy(out + 40, &pcmBytes, 4);
		std::memcpy(out + 44, scaledPcm.data(), pcmBytes);

		::PlaySoundA(reinterpret_cast<LPCSTR>(s_currentPreviewMemory.data()), NULL, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
	}
}

void SafeConPrintf(const char* fmt, ...);

static bool IsVoiceReady()
{
	if (!VoiceExtension())
		return false;

	if (!s_listenerRegistered && VoiceExtension_IsLoaded())
	{
		SoundEngine_Init();
	}

	// API_002+ knows whether the engine hooks are installed and the client is
	// actually in state 5. API_001 has no readiness query, so keep the local
	// player heuristic for that legacy interface only.
	if (IVoiceExtension2* pVoice2 = VoiceExtension2())
		return pVoice2->IsReady();

	return SoundEngine_IsConnected();
}

void SoundEngine_Init()
{
	if (s_listenerRegistered)
	{
		DebuLog("SoundEngine_Init: listener already registered, skipping duplicate init");
		return;
	}

	DebuLog("SoundEngine_Init: initializing SoundEngine and VoiceExtension bindings");
	VoiceExtension_Init();

	IVoiceExtension3* pVoice3 = VoiceExtension3();
	if (pVoice3)
	{
		VoiceClientParams cp;
		cp.hangtimeMs = 0; // no artificial tail: go IDLE as soon as the queue is empty
		s_soundpadClientId = pVoice3->RegisterClient("Soundpad", cp);
		if (s_soundpadClientId != 0)
		{
			SafeConPrintf("[Soundpad] VoiceExtension loaded successfully (API 003, clientId=%u). Voice broadcasting ready.\n", s_soundpadClientId);
			OutputDebugStringA("[Soundpad] VoiceExtension loaded successfully. Voice broadcasting ready.\n");
		}
		else
		{
			SafeConPrintf("[Soundpad] WARNING: VoiceExtension RegisterClient failed; client isolation is unavailable.\n");
			OutputDebugStringA("[Soundpad] WARNING: VoiceExtension RegisterClient failed.\n");
		}

		pVoice3->RegisterListener(&s_voiceListener);
		s_listenerRegistered = true;
		return;
	}

	if (IVoiceExtension* pVoice = VoiceExtension())
	{
		s_soundpadClientId = 0;
		SafeConPrintf("[Soundpad] VoiceExtension loaded successfully. Voice broadcasting ready.\n");
		OutputDebugStringA("[Soundpad] VoiceExtension loaded successfully. Voice broadcasting ready.\n");
		pVoice->RegisterListener(&s_voiceListener);
		s_listenerRegistered = true;
		return;
	}

	s_soundpadClientId = 0;
	SafeConPrintf("[Soundpad] WARNING: VoiceExtension.dll not loaded. Soundpad will run in local preview mode only.\n");
	OutputDebugStringA("[Soundpad] WARNING: VoiceExtension.dll not loaded. Soundpad will run in local preview mode only.\n");
}

void SoundEngine_Shutdown()
{
	DebuLog("SoundEngine_Shutdown entered");
	SoundEngine_Stop();

	if (s_listenerRegistered)
	{
		if (IVoiceExtension3* pVoice3 = VoiceExtension3())
		{
			pVoice3->UnregisterListener(&s_voiceListener);
			if (s_soundpadClientId != 0)
				pVoice3->UnregisterClient(s_soundpadClientId);
		}
		else if (IVoiceExtension* pVoice = VoiceExtension())
		{
			pVoice->UnregisterListener(&s_voiceListener);
		}
	}

	s_listenerRegistered = false;
	s_soundpadClientId = 0;
	VoiceExtension_Shutdown();
}

void SoundEngine_OnVoiceExtensionUnloaded()
{
	DebuLog("SoundEngine_OnVoiceExtensionUnloaded: detaching without calling into the unloading module");
	{
		std::lock_guard<std::mutex> lock(s_monitorMutex);
		s_monitorTracks.clear();
		s_coreToCallerTrackId.clear();
	}
	{
		std::lock_guard<std::mutex> lock(s_statusMutex);
		s_activeTrackName.clear();
	}
	s_soundpadClientId = 0;
	s_listenerRegistered = false;
	VoiceExtension_Shutdown();
}

bool SoundEngine_IsVoiceBound()
{
	return s_listenerRegistered;
}

bool SoundEngine_Play(const char* relativePath, float volume, float pitch, bool enqueue)
{
	SoundPlayParams p;
	p.volume = volume;
	p.pitch = pitch;
	p.enqueue = enqueue;
	return SoundEngine_PlayEx(relativePath, p);
}

bool SoundEngine_PlayEx(const char* relativePath, const SoundPlayParams& params)
{
	if (!relativePath || !*relativePath || g_isMuted)
		return false;

	DebuLog("SoundEngine_PlayEx: path='%s', vol=%.2f, pitch=%.2f, prioCutIn=%d, localPreview=%d, enqueue=%d",
		relativePath, params.volume, params.pitch, params.priorityCutIn ? 1 : 0, params.localPreviewOnly, params.enqueue);

	if (params.localPreviewOnly)
	{
		SoundEngine_PreviewLocal(relativePath, params.volume, params.pitch);
		return true;
	}

	IVoiceExtension* pVoice = VoiceExtension();
	if (!pVoice)
	{
		SafeConPrintf("[Soundpad] Cannot broadcast sound: VoiceExtension.dll is not active, playing locally.\n");
		SoundEngine_PreviewLocal(relativePath, params.volume, params.pitch);
		return true;
	}

	IVoiceExtension2* pVoice2 = VoiceExtension2();
	if (pVoice2 && pVoice2->IsPanic())
	{
		SafeConPrintf("[Soundpad] VoiceExtension is in PANIC mode (%s), playing locally.\n", pVoice2->GetPanicReason());
		SoundEngine_PreviewLocal(relativePath, params.volume, params.pitch);
		return true;
	}

	if (!s_listenerRegistered && VoiceExtension_IsLoaded())
	{
		SoundEngine_Init();
	}

	if (!IsVoiceReady())
	{
		IVoiceExtension2* pV2 = VoiceExtension2();
		SafeConPrintf("[Soundpad] Voice broadcast not ready (pVoice=%p, isReady=%d, connected=%d), playing locally via PreviewLocal.\n",
			pVoice, pV2 ? (pV2->IsReady() ? 1 : 0) : -1, SoundEngine_IsConnected() ? 1 : 0);
		DebuLog("SoundEngine_PlayEx: VoiceExtension is not ready (hooks/client state), routing to local preview only");
		SoundEngine_PreviewLocal(relativePath, params.volume, params.pitch);
		return true;
	}

	if (!params.enqueue)
	{
		if (VoiceExtension3() && s_soundpadClientId != 0)
			VoiceExtension3()->StopClient(s_soundpadClientId);
		else
			pVoice->StopCurrent();
		{
			std::lock_guard<std::mutex> lock(s_monitorMutex);
			s_monitorTracks.clear();
			s_coreToCallerTrackId.clear();
		}
		{
			std::lock_guard<std::mutex> lock(s_previewMutex);
			::PlaySoundA(NULL, NULL, 0);
		}
	}

	VoicePlayParams vp = ConvertParams(params);

	// voice_loopback already returns the outgoing voice to the local client, so a
	// local monitor on top of it doubles the sound. Monitor only when loopback is off.
	cvar_t* pLoopback = gEngfuncs.pfnGetCvarPointer ? gEngfuncs.pfnGetCvarPointer("voice_loopback") : nullptr;
	bool needLocalMonitor = (!pLoopback || pLoopback->value <= 0.0f);

	uint32_t myTrackId = 0;
	if (needLocalMonitor)
	{
		myTrackId = s_nextCallerTrackId.fetch_add(1);
		vp.trackId = myTrackId;

		std::lock_guard<std::mutex> lock(s_monitorMutex);
		MonitorAudio mon;
		// Do not read file synchronously on game thread! Only store path and parameters.
		// Local playback is offloaded to the worker thread upon OnTrackStarted.
		mon.filePath = relativePath;
		mon.volume = params.volume;
		mon.pitch = params.pitch;
		s_monitorTracks[myTrackId] = std::move(mon);
	}

#if SOUNDPAD_ENABLE_DSP
	bool hasDsp = (params.fxReverb || params.fxZigzag || params.fadeInSec > 0.0f ||
		params.fadeOutSec > 0.0f || params.startSec > 0.0f || params.endSec > 0.0f);
#else
	// Slicing still needs the PCM path; fade/reverb/zigzag are asleep.
	bool hasDsp = (params.startSec > 0.0f || params.endSec > 0.0f);
#endif

	SP_DEBUG_LOG("[Soundpad] Enqueue sound '%s' (enqueue=%d, callerTrackId=%u, clientId=%u)\n",
		relativePath, params.enqueue ? 1 : 0, myTrackId, s_soundpadClientId);

	IVoiceExtension3* pVoice3 = VoiceExtension3();
	uint32_t queuedId = 0;
	if (!hasDsp)
	{
		if (pVoice3 && s_soundpadClientId != 0)
			queuedId = pVoice3->EnqueueFileEx(s_soundpadClientId, relativePath, vp);
		else
			queuedId = pVoice->EnqueueFile(relativePath, vp);
	}
	else
	{
		// DSP requested: attempt reading WAV PCM directly
		std::vector<int16_t> pcm;
		int sampleRate = 0;
		if (ReadPcmWav(relativePath, pcm, sampleRate) && !pcm.empty() && sampleRate > 0)
		{
			std::vector<float> mono(pcm.size());
			for (size_t i = 0; i < pcm.size(); ++i)
				mono[i] = static_cast<float>(pcm[i]) / 32768.0f;

			ApplyDspEffects(mono, sampleRate, params);
			if (!mono.empty())
			{
				std::vector<int16_t> outPcm;
				FloatToPcm16(mono, outPcm);
				if (needLocalMonitor && myTrackId != 0)
				{
					std::lock_guard<std::mutex> lock(s_monitorMutex);
					s_monitorTracks[myTrackId].pcm = outPcm;
					s_monitorTracks[myTrackId].sampleRate = sampleRate;
				}
				if (pVoice3 && s_soundpadClientId != 0)
					queuedId = pVoice3->EnqueuePcmEx(s_soundpadClientId, outPcm.data(), outPcm.size(), sampleRate, vp);
				else
					queuedId = pVoice->EnqueuePcm(outPcm.data(), outPcm.size(), sampleRate, vp);
			}
		}
		else
		{
			if (pVoice3 && s_soundpadClientId != 0)
				queuedId = pVoice3->EnqueueFileEx(s_soundpadClientId, relativePath, vp);
			else
				queuedId = pVoice->EnqueueFile(relativePath, vp);
		}
	}

	SP_DEBUG_LOG("[Soundpad] Enqueued result: queuedId=0x%08X (callerTrackId=%u)\n", queuedId, myTrackId);

	if (queuedId == 0 && myTrackId != 0)
	{
		std::lock_guard<std::mutex> lock(s_monitorMutex);
		s_monitorTracks.erase(myTrackId);
		return false;
	}
	else if (queuedId != 0 && myTrackId != 0)
	{
		std::lock_guard<std::mutex> lock(s_monitorMutex);
		s_coreToCallerTrackId[queuedId] = myTrackId;
	}

	return (queuedId != 0);
}

bool SoundEngine_PlayPCM(const int16_t* pcm, size_t numSamples, int sampleRate, const SoundPlayParams& params, const char* identifier)
{
	if (!pcm || numSamples == 0 || sampleRate <= 0 || g_isMuted)
		return false;

	if (params.localPreviewOnly)
	{
		std::vector<int16_t> pcmVec(pcm, pcm + numSamples);
		PlayWavFromMemory(pcmVec, sampleRate, params.volume, params.pitch);
		return true;
	}

	IVoiceExtension* pVoice = VoiceExtension();
	if (!pVoice)
	{
		SafeConPrintf("[Soundpad] Cannot broadcast PCM: VoiceExtension.dll is not active, playing locally.\n");
		std::vector<int16_t> pcmVec(pcm, pcm + numSamples);
		PlayWavFromMemory(pcmVec, sampleRate, params.volume, params.pitch);
		return true;
	}

	IVoiceExtension2* pVoice2 = VoiceExtension2();
	if (pVoice2 && pVoice2->IsPanic())
	{
		SafeConPrintf("[Soundpad] VoiceExtension is in PANIC mode (%s), playing locally.\n", pVoice2->GetPanicReason());
		std::vector<int16_t> pcmVec(pcm, pcm + numSamples);
		PlayWavFromMemory(pcmVec, sampleRate, params.volume, params.pitch);
		return true;
	}

	if (!s_listenerRegistered && VoiceExtension_IsLoaded())
	{
		SoundEngine_Init();
	}

	if (!IsVoiceReady())
	{
		IVoiceExtension2* pV2 = VoiceExtension2();
		SafeConPrintf("[Soundpad] Voice broadcast not ready for PCM (pVoice=%p, isReady=%d, connected=%d), playing locally.\n",
			pVoice, pV2 ? (pV2->IsReady() ? 1 : 0) : -1, SoundEngine_IsConnected() ? 1 : 0);
		DebuLog("SoundEngine_PlayPCM: VoiceExtension is not ready (hooks/client state), routing to local preview only");
		std::vector<int16_t> pcmVec(pcm, pcm + numSamples);
		PlayWavFromMemory(pcmVec, sampleRate, params.volume, params.pitch);
		return true;
	}

	if (!params.enqueue)
	{
		if (VoiceExtension3() && s_soundpadClientId != 0)
			VoiceExtension3()->StopClient(s_soundpadClientId);
		else
			pVoice->StopCurrent();
		{
			std::lock_guard<std::mutex> lock(s_monitorMutex);
			s_monitorTracks.clear();
			s_coreToCallerTrackId.clear();
		}
		{
			std::lock_guard<std::mutex> lock(s_previewMutex);
			::PlaySoundA(NULL, NULL, 0);
		}
	}

	VoicePlayParams vp = ConvertParams(params);

	// voice_loopback already returns the outgoing voice to the local client, so a
	// local monitor on top of it doubles the sound. Monitor only when loopback is off.
	cvar_t* pLoopback = gEngfuncs.pfnGetCvarPointer ? gEngfuncs.pfnGetCvarPointer("voice_loopback") : nullptr;
	bool needLocalMonitor = (!pLoopback || pLoopback->value <= 0.0f);

	uint32_t myTrackId = 0;
	if (needLocalMonitor)
	{
		myTrackId = s_nextCallerTrackId.fetch_add(1);
		vp.trackId = myTrackId;

		std::lock_guard<std::mutex> lock(s_monitorMutex);
		MonitorAudio mon;
		mon.pcm.assign(pcm, pcm + numSamples);
		mon.sampleRate = sampleRate;
		mon.volume = params.volume;
		mon.pitch = params.pitch;
		s_monitorTracks[myTrackId] = std::move(mon);
	}

#if SOUNDPAD_ENABLE_DSP
	bool hasDsp = (params.fxReverb || params.fxZigzag || params.fadeInSec > 0.0f ||
		params.fadeOutSec > 0.0f || params.startSec > 0.0f || params.endSec > 0.0f);
#else
	// Slicing still needs the PCM path; fade/reverb/zigzag are asleep.
	bool hasDsp = (params.startSec > 0.0f || params.endSec > 0.0f);
#endif

	IVoiceExtension3* pVoice3 = VoiceExtension3();
	uint32_t queuedId = 0;
	if (!hasDsp)
	{
		if (pVoice3 && s_soundpadClientId != 0)
			queuedId = pVoice3->EnqueuePcmEx(s_soundpadClientId, pcm, numSamples, sampleRate, vp);
		else
			queuedId = pVoice->EnqueuePcm(pcm, numSamples, sampleRate, vp);
	}
	else
	{
		std::vector<float> mono(numSamples);
		for (size_t i = 0; i < numSamples; ++i)
			mono[i] = static_cast<float>(pcm[i]) / 32768.0f;

		ApplyDspEffects(mono, sampleRate, params);
		if (mono.empty())
		{
			if (myTrackId != 0)
			{
				std::lock_guard<std::mutex> lock(s_monitorMutex);
				s_monitorTracks.erase(myTrackId);
			}
			return false;
		}

		std::vector<int16_t> outPcm;
		FloatToPcm16(mono, outPcm);
		if (pVoice3 && s_soundpadClientId != 0)
			queuedId = pVoice3->EnqueuePcmEx(s_soundpadClientId, outPcm.data(), outPcm.size(), sampleRate, vp);
		else
			queuedId = pVoice->EnqueuePcm(outPcm.data(), outPcm.size(), sampleRate, vp);
	}

	if (queuedId == 0 && myTrackId != 0)
	{
		std::lock_guard<std::mutex> lock(s_monitorMutex);
		s_monitorTracks.erase(myTrackId);
		return false;
	}
	else if (queuedId != 0 && myTrackId != 0)
	{
		std::lock_guard<std::mutex> lock(s_monitorMutex);
		s_coreToCallerTrackId[queuedId] = myTrackId;
	}

	return (queuedId != 0);
}

bool SoundEngine_SpeakTTS(const char* text, float volume, float pitch, bool repoRandomPitch, bool whisper, bool localOnly, bool priorityInterleave, bool randomStyle, bool enqueue)
{
	if (!text || !*text || g_isMuted)
		return false;

	// Limit text length to avoid memory inflation from chat commands
	std::string textStr = text;
	if (textStr.size() > 500)
		textStr.resize(500);

	// Offload speech synthesis to worker thread so the game thread / chat hook never freezes
	std::thread([textStr = std::move(textStr), volume, pitch, repoRandomPitch, whisper, localOnly, priorityInterleave, randomStyle, enqueue]() {
		const char* engineName = "klatter";
		if (sp_tts_engine && sp_tts_engine->string && sp_tts_engine->string[0])
			engineName = sp_tts_engine->string;
		else
			engineName = CSoundpadConfig::Get().GetTtsEngine().c_str();

		bool isSapi = (engineName && (_stricmp(engineName, "sapi") == 0 ||
		                             _stricmp(engineName, "1") == 0 ||
		                             _stricmp(engineName, "sapi5") == 0 ||
		                             _strnicmp(engineName, "sapi", 4) == 0));

		std::vector<int16_t> pcm;
		int sampleRate = 11025;
		const char* sourceTag = "KlatterTTS";

		if (isSapi)
		{
			Sapi::SapiConfig scfg;
			scfg.volume = volume;
			scfg.rate = (pitch - 1.0f) * 10.0f;
			if (sp_sapi_voice && sp_sapi_voice->string && sp_sapi_voice->string[0])
				scfg.voiceName = sp_sapi_voice->string;
			else
				scfg.voiceName = CSoundpadConfig::Get().GetSapiVoice();

			pcm = Sapi::SynthesizeText(textStr, sampleRate, scfg);
			sourceTag = "SapiTTS";

			if (pcm.empty())
			{
				SafeConPrintf("[Soundpad] SAPI TTS failed or produced no audio, falling back to Klatter.\n");
				DebuLog("SoundEngine_SpeakTTS: SAPI produced 0 samples, fallback to Klatter");

				Klatter::KlatterConfig cfg;
				cfg.baseF0 = 220.0f * pitch;
				if (cfg.baseF0 < 50.0f) cfg.baseF0 = 50.0f;
				if (cfg.baseF0 > 600.0f) cfg.baseF0 = 600.0f;
				cfg.mode = whisper ? Klatter::VoicingMode::Whisper : Klatter::VoicingMode::Natural;
				cfg.repoJumpingPitch = repoRandomPitch;
				cfg.randomStyleAtStart = randomStyle;

				Klatter::KlatterEngine engine;
				pcm = engine.SynthesizeText(textStr.c_str(), cfg);
				sampleRate = 11025;
				sourceTag = "KlatterTTS";
			}
		}
		else
		{
			Klatter::KlatterConfig cfg;
			cfg.baseF0 = 220.0f * pitch;
			if (cfg.baseF0 < 50.0f) cfg.baseF0 = 50.0f;
			if (cfg.baseF0 > 600.0f) cfg.baseF0 = 600.0f;
			cfg.mode = whisper ? Klatter::VoicingMode::Whisper : Klatter::VoicingMode::Natural;
			cfg.repoJumpingPitch = repoRandomPitch;
			cfg.randomStyleAtStart = randomStyle;

			Klatter::KlatterEngine engine;
			pcm = engine.SynthesizeText(textStr.c_str(), cfg);
			sampleRate = 11025;
			sourceTag = "KlatterTTS";
		}

		if (pcm.empty())
		{
			SafeConPrintf("[Soundpad] TTS synthesis returned 0 samples.\n");
			return;
		}

		if (localOnly)
		{
			PlayWavFromMemory(pcm, sampleRate, volume, 1.0f);
			return;
		}

		SoundPlayParams sp;
		sp.volume = volume;
		sp.pitch = 1.0f;
		sp.priorityCutIn = priorityInterleave;
		sp.isTTS = true;
		sp.enqueue = enqueue;

		SoundEngine_PlayPCM(pcm.data(), pcm.size(), sampleRate, sp, sourceTag);
	}).detach();

	return true;
}

float SoundEngine_GetWaveDuration(const char* relativePath)
{
	if (!relativePath || !*relativePath)
		return 0.0f;

	return ReadWavDuration(relativePath);
}

void SoundEngine_Stop()
{
	IVoiceExtension3* pVoice3 = VoiceExtension3();
	if (pVoice3 && s_soundpadClientId != 0)
	{
		pVoice3->StopClient(s_soundpadClientId);
	}
	else if (pVoice3)
	{
		// API_003 is present but client registration failed. Do not call
		// StopAll here: that would clear every other VE client's queue.
		pVoice3->StopCurrent();
	}
	else if (IVoiceExtension* pVoice = VoiceExtension())
	{
		pVoice->StopAll();
	}
	{
		std::lock_guard<std::mutex> lock(s_monitorMutex);
		s_monitorTracks.clear();
		s_coreToCallerTrackId.clear();
	}
	{
		std::lock_guard<std::mutex> lock(s_previewMutex);
		::PlaySoundA(NULL, NULL, 0);
	}
}

bool SoundEngine_StopTrack(uint32_t trackId)
{
	IVoiceExtension3* pVoice3 = VoiceExtension3();
	if (pVoice3)
	{
		uint32_t coreId = trackId;
		{
			std::lock_guard<std::mutex> lock(s_monitorMutex);
			for (const auto& pair : s_coreToCallerTrackId)
			{
				if (pair.second == trackId)
				{
					coreId = pair.first;
					break;
				}
			}
		}
		bool ok = pVoice3->StopTrack(coreId);
		{
			std::lock_guard<std::mutex> lock(s_monitorMutex);
			auto it = s_coreToCallerTrackId.find(coreId);
			if (it != s_coreToCallerTrackId.end())
			{
				s_monitorTracks.erase(it->second);
				s_coreToCallerTrackId.erase(it);
			}
			s_monitorTracks.erase(coreId);
			s_monitorTracks.erase(trackId);
		}
		return ok;
	}
	return false;
}

void SoundEngine_PreviewLocal(const char* relativePath, float volume, float pitch, bool isMonitor)
{
	if (!relativePath || !*relativePath || g_isMuted)
		return;

	// In-memory preview plays audio directly, completely bypassing GoldSrc's sfx_t cache!
	// Supports any WAV format (48kHz, 44.1kHz, 22kHz, stereo, float, 16/24/32-bit).
	std::vector<int16_t> pcm;
	int sampleRate = 0;
	if (ReadPcmWav(relativePath, pcm, sampleRate) && !pcm.empty() && sampleRate > 0)
	{
		PlayWavFromMemory(pcm, sampleRate, volume, pitch);
		return;
	}

	// MP3 fallback: the engine lookup below is WAV-only, so decode MP3 to PCM here.
	if (ReadMp3Pcm(relativePath, pcm, sampleRate) && !pcm.empty() && sampleRate > 0)
	{
		PlayWavFromMemory(pcm, sampleRate, volume, pitch);
		return;
	}

	// Fallback to direct engine playback if file couldn't be loaded into raw memory
	int pitchInt = static_cast<int>(pitch * 100.0f);
	if (pitchInt < 10) pitchInt = 10;
	if (pitchInt > 250) pitchInt = 250;
	float vol = (std::max)(0.01f, (std::min)(1.0f, volume * g_masterVolume));

	std::string clean = relativePath;
	for (char& c : clean)
	{
		if (c == '\\') c = '/';
	}
	if (clean.rfind("sound/", 0) == 0)
		clean = clean.substr(6);

	if (gEngfuncs.pfnPlaySoundByNameAtPitch)
	{
		gEngfuncs.pfnPlaySoundByNameAtPitch(const_cast<char*>(clean.c_str()), vol, pitchInt);
	}
	else if (gEngfuncs.pfnClientCmd)
	{
		char cmd[MAX_PATH + 32] = {};
		StringCchPrintfA(cmd, sizeof(cmd), "playvol \"%s\" %.2f\n", clean.c_str(), vol);
		gEngfuncs.pfnClientCmd(cmd);
	}
}

void SoundEngine_PreviewSound(const char* relativePath, float volume, float pitch, bool isMonitor)
{
	SoundEngine_PreviewLocal(relativePath, volume, pitch, isMonitor);
}

bool SoundEngine_IsConnected()
{
	if (!gEngfuncs.GetLocalPlayer)
		return false;

	cl_entity_t* localEnt = gEngfuncs.GetLocalPlayer();
	if (!localEnt || localEnt->player == 0 || localEnt->index <= 0)
		return false;

	return true;
}

void SoundEngine_OnHudFrame(double time)
{
	static double s_lastDisconnectCheck = 0.0;
	if (time - s_lastDisconnectCheck < 0.25)
		return;
	s_lastDisconnectCheck = time;

	IVoiceExtension* pVoice = VoiceExtension();
	if (pVoice && pVoice->IsTransmitting())
	{
		if (!SoundEngine_IsConnected())
		{
			DebuLog("SoundEngine_OnHudFrame: local player disconnected or SteamID became 0 during transmission, stopping playback");
			SoundEngine_Stop();
		}
	}
}

PlaybackStatus SoundEngine_GetStatus()
{
	PlaybackStatus st;
	IVoiceExtension* pVoice = VoiceExtension();
	if (pVoice)
	{
		VoicePlaybackState state = pVoice->GetState();
		st.isPlaying = (state == VoicePlaybackState::PLAYING || state == VoicePlaybackState::PREROLL || state == VoicePlaybackState::POSTROLL);
		st.isDucking = (state == VoicePlaybackState::DUCKED);
		st.queueSize = pVoice->GetQueueSize();
	}
	{
		std::lock_guard<std::mutex> lock(s_statusMutex);
		st.currentSound = s_activeTrackName;
	}
	st.volume = g_masterVolume;
	return st;
}

void SoundEngine_SetMasterVolume(float vol)
{
	g_masterVolume = std::clamp(vol, 0.0f, 2.0f);
}

float SoundEngine_GetMasterVolume()
{
	return g_masterVolume;
}

void SoundEngine_SetMuted(bool mute)
{
	g_isMuted = mute;
	if (g_isMuted)
	{
		SoundEngine_Stop();
	}
}

bool SoundEngine_IsMuted()
{
	return g_isMuted;
}

void SoundEngine_DumpReport()
{
	IVoiceExtension* pVoice = VoiceExtension();
	SafeConPrintf("=== Soundpad Voice Status ===\n");
	SafeConPrintf("Voice Backend: %s\n", pVoice ? "VoiceExtension.dll (Active)" : "None (VoiceExtension not loaded)");
	SafeConPrintf("Transmitting: %s, Muted: %s, MasterVol: %.2f\n",
		(pVoice && pVoice->IsTransmitting()) ? "YES" : "NO",
		g_isMuted ? "YES" : "NO", g_masterVolume);
	SafeConPrintf("=============================\n");
}
