#include "SapiEngine.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <initguid.h>
#include <sapi.h>
#include <ole2.h>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include "DebugLog.h"

#ifndef __MINGW32__
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#endif

void SafeConPrintf(const char* fmt, ...);

namespace Sapi
{

// Exact Microsoft Speech API 5 GUID definitions:
// CLSID_SpVoice:               {96749377-3391-11D2-9EE3-00C04F797396}
// IID_ISpVoice:                {6C44DF74-72B9-4992-A1EC-EF996E0422D4}
// CLSID_SpStream:              {715D9C59-4442-11D2-9605-00C04F8EE628}
// IID_ISpStream:               {12E3CCA9-7518-44C5-A5E7-BA5A79CB929E}
// SPDFID_WaveFormatEx:         {C31ADBAE-527F-4FF5-A230-F62BB61FF70C}
// CLSID_SpObjectTokenCategory: {A910187F-0C7A-45AC-92CC-59EDAFB77B53}
// IID_ISpObjectTokenCategory:  {2D3D3845-39AF-4850-BBF9-40B49780011D}
static const GUID kCLSID_SpVoice               = { 0x96749377, 0x3391, 0x11D2, { 0x9E, 0xE3, 0x00, 0xC0, 0x4F, 0x79, 0x73, 0x96 } };
static const GUID kIID_ISpVoice                = { 0x6C44DF74, 0x72B9, 0x4992, { 0xA1, 0xEC, 0xEF, 0x99, 0x6E, 0x04, 0x22, 0xD4 } };
static const GUID kCLSID_SpStream              = { 0x715D9C59, 0x4442, 0x11D2, { 0x96, 0x05, 0x00, 0xC0, 0x4F, 0x8E, 0xE6, 0x28 } };
static const GUID kIID_ISpStream               = { 0x12E3CCA9, 0x7518, 0x44C5, { 0xA5, 0xE7, 0xBA, 0x5A, 0x79, 0xCB, 0x92, 0x9E } };
static const GUID kSPDFID_WaveFormatEx         = { 0xC31ADBAE, 0x527F, 0x4FF5, { 0xA2, 0x30, 0xF6, 0x2B, 0xB6, 0x1F, 0xF7, 0x0C } };
static const GUID kCLSID_SpObjectTokenCategory = { 0xA910187F, 0x0C7A, 0x45AC, { 0x92, 0xCC, 0x59, 0xED, 0xAF, 0xB7, 0x7B, 0x53 } };
static const GUID kIID_ISpObjectTokenCategory  = { 0x2D3D3845, 0x39AF, 0x4850, { 0xBB, 0xF9, 0x40, 0xB4, 0x97, 0x80, 0x01, 0x1D } };

static void LogSapi(const char* fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);
	OutputDebugStringA(buf);
	::SafeConPrintf("%s", buf);
}

struct ScopedCOM
{
	HRESULT hr;
	bool needUninit;

	ScopedCOM() : hr(S_OK), needUninit(false)
	{
		hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		if (SUCCEEDED(hr))
		{
			needUninit = true;
		}
		else if (hr == RPC_E_CHANGED_MODE)
		{
			// COM is already initialized in MTA mode on this thread; keep going without uninit
			needUninit = false;
		}
		else
		{
			hr = CoInitialize(NULL);
			if (SUCCEEDED(hr))
			{
				needUninit = true;
			}
		}
	}

	~ScopedCOM()
	{
		if (needUninit)
		{
			CoUninitialize();
		}
	}
};

static std::wstring ToWideString(const std::string& input)
{
	if (input.empty())
		return std::wstring();

	std::string clean = input;
	// Trim trailing incomplete multi-byte sequence caused by buffer truncation
	while (!clean.empty())
	{
		uint8_t last = static_cast<uint8_t>(clean.back());
		if ((last & 0x80) == 0)
			break; // Standard ASCII char

		if ((last & 0xC0) == 0xC0)
		{
			// Leading byte missing its continuation byte(s)
			clean.pop_back();
			break;
		}
		break;
	}

	// 1. Try UTF-8 with strict validation
	int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, clean.c_str(), static_cast<int>(clean.size()), NULL, 0);
	if (needed > 0)
	{
		std::wstring result(needed, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, clean.c_str(), static_cast<int>(clean.size()), &result[0], needed);
		return result;
	}

	// 2. Fallback to CP1251 (standard Russian ANSI in GoldSrc)
	needed = MultiByteToWideChar(1251, 0, input.c_str(), static_cast<int>(input.size()), NULL, 0);
	if (needed > 0)
	{
		std::wstring result(needed, L'\0');
		MultiByteToWideChar(1251, 0, input.c_str(), static_cast<int>(input.size()), &result[0], needed);
		return result;
	}

	// 3. Fallback to CP_ACP
	needed = MultiByteToWideChar(CP_ACP, 0, input.c_str(), static_cast<int>(input.size()), NULL, 0);
	if (needed > 0)
	{
		std::wstring result(needed, L'\0');
		MultiByteToWideChar(CP_ACP, 0, input.c_str(), static_cast<int>(input.size()), &result[0], needed);
		return result;
	}

	return std::wstring();
}

bool IsAvailable()
{
	ScopedCOM com;
	ISpVoice* pVoice = nullptr;
	HRESULT hr = CoCreateInstance(kCLSID_SpVoice, NULL, CLSCTX_ALL, kIID_ISpVoice, (void**)&pVoice);
	if (SUCCEEDED(hr) && pVoice)
	{
		pVoice->Release();
		return true;
	}
	return false;
}

std::vector<std::string> GetAvailableVoices()
{
	std::vector<std::string> voices;
	ScopedCOM com;

	ISpObjectTokenCategory* pCategory = nullptr;
	HRESULT hr = CoCreateInstance(kCLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, kIID_ISpObjectTokenCategory, (void**)&pCategory);
	if (SUCCEEDED(hr) && pCategory)
	{
		if (SUCCEEDED(pCategory->SetId(SPCAT_VOICES, FALSE)))
		{
			IEnumSpObjectTokens* pEnum = nullptr;
			if (SUCCEEDED(pCategory->EnumTokens(NULL, NULL, &pEnum)) && pEnum)
			{
				ULONG count = 0;
				pEnum->GetCount(&count);
				for (ULONG i = 0; i < count; ++i)
				{
					ISpObjectToken* pToken = nullptr;
					if (SUCCEEDED(pEnum->Next(1, &pToken, NULL)) && pToken)
					{
						WCHAR* pDesc = nullptr;
						if (SUCCEEDED(pToken->GetStringValue(NULL, &pDesc)) && pDesc)
						{
							int len = WideCharToMultiByte(CP_UTF8, 0, pDesc, -1, NULL, 0, NULL, NULL);
							if (len > 0)
							{
								std::string name(len - 1, '\0');
								WideCharToMultiByte(CP_UTF8, 0, pDesc, -1, &name[0], len, NULL, NULL);
								voices.push_back(name);
							}
							CoTaskMemFree(pDesc);
						}
						pToken->Release();
					}
				}
				pEnum->Release();
			}
		}
		pCategory->Release();
	}
	return voices;
}

std::vector<int16_t> SynthesizeText(const std::string& text, int& outSampleRate, const SapiConfig& cfg)
{
	std::vector<int16_t> pcm;
	outSampleRate = 22050;

	if (text.empty())
		return pcm;

	std::wstring wtext = ToWideString(text);
	if (wtext.empty())
	{
		DebuLog("SAPI: ToWideString produced empty wide string");
		LogSapi("[Soundpad SAPI] ToWideString returned empty string for input.\n");
		return pcm;
	}

	// Limit text length to prevent memory abuse
	if (wtext.size() > 500)
		wtext.resize(500);

	ScopedCOM com;

	// 1. Create in-memory stream backed by HGLOBAL
	IStream* pMemStream = nullptr;
	HRESULT hr = CreateStreamOnHGlobal(NULL, TRUE, &pMemStream);
	if (FAILED(hr) || !pMemStream)
	{
		DebuLog("SAPI: CreateStreamOnHGlobal failed hr=0x%08X", hr);
		LogSapi("[Soundpad SAPI] CreateStreamOnHGlobal failed: hr=0x%08X\n", hr);
		return pcm;
	}

	// 2. Create SAPI stream wrapper
	ISpStream* pSpStream = nullptr;
	hr = CoCreateInstance(kCLSID_SpStream, NULL, CLSCTX_ALL, kIID_ISpStream, (void**)&pSpStream);
	if (FAILED(hr) || !pSpStream)
	{
		DebuLog("SAPI: CoCreateInstance SpStream failed hr=0x%08X", hr);
		LogSapi("[Soundpad SAPI] CoCreateInstance SpStream failed: hr=0x%08X\n", hr);
		pMemStream->Release();
		return pcm;
	}

	// 3. Configure audio format (22050 Hz, 16-bit, Mono PCM)
	WAVEFORMATEX wfx = {};
	wfx.wFormatTag = WAVE_FORMAT_PCM;
	wfx.nChannels = 1;
	wfx.nSamplesPerSec = 22050;
	wfx.wBitsPerSample = 16;
	wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
	wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
	wfx.cbSize = 0;

	hr = pSpStream->SetBaseStream(pMemStream, kSPDFID_WaveFormatEx, &wfx);
	if (FAILED(hr))
	{
		DebuLog("SAPI: SetBaseStream failed hr=0x%08X", hr);
		LogSapi("[Soundpad SAPI] SetBaseStream failed: hr=0x%08X\n", hr);
		pSpStream->Release();
		pMemStream->Release();
		return pcm;
	}

	// 4. Create SAPI voice instance
	ISpVoice* pVoice = nullptr;
	hr = CoCreateInstance(kCLSID_SpVoice, NULL, CLSCTX_ALL, kIID_ISpVoice, (void**)&pVoice);
	if (FAILED(hr) || !pVoice)
	{
		DebuLog("SAPI: CoCreateInstance SpVoice failed hr=0x%08X", hr);
		LogSapi("[Soundpad SAPI] CoCreateInstance SpVoice failed: hr=0x%08X\n", hr);
		pSpStream->Release();
		pMemStream->Release();
		return pcm;
	}

	// 5. Select voice (explicit configuration or Cyrillic auto-detection)
	std::string targetVoice = cfg.voiceName;
	bool hasCyrillic = false;
	for (wchar_t wc : wtext)
	{
		if (wc >= 0x0400 && wc <= 0x04FF)
		{
			hasCyrillic = true;
			break;
		}
	}

	ISpObjectTokenCategory* pCat = nullptr;
	if (SUCCEEDED(CoCreateInstance(kCLSID_SpObjectTokenCategory, NULL, CLSCTX_ALL, kIID_ISpObjectTokenCategory, (void**)&pCat)) && pCat)
	{
		if (SUCCEEDED(pCat->SetId(SPCAT_VOICES, FALSE)))
		{
			IEnumSpObjectTokens* pEnum = nullptr;
			if (SUCCEEDED(pCat->EnumTokens(NULL, NULL, &pEnum)) && pEnum)
			{
				ULONG count = 0;
				pEnum->GetCount(&count);
				ISpObjectToken* pSelectedToken = nullptr;

				for (ULONG i = 0; i < count; ++i)
				{
					ISpObjectToken* pToken = nullptr;
					if (SUCCEEDED(pEnum->Next(1, &pToken, NULL)) && pToken)
					{
						WCHAR* pDesc = nullptr;
						if (SUCCEEDED(pToken->GetStringValue(NULL, &pDesc)) && pDesc)
						{
							int len = WideCharToMultiByte(CP_UTF8, 0, pDesc, -1, NULL, 0, NULL, NULL);
							if (len > 0)
							{
								std::string vName(len - 1, '\0');
								WideCharToMultiByte(CP_UTF8, 0, pDesc, -1, &vName[0], len, NULL, NULL);

								if (!targetVoice.empty() && vName.find(targetVoice) != std::string::npos)
								{
									pSelectedToken = pToken;
									CoTaskMemFree(pDesc);
									break;
								}
								else if (targetVoice.empty() && hasCyrillic)
								{
									// Look for Russian voice indicators in 32-bit registry
									if (vName.find("Russian") != std::string::npos ||
									    vName.find("Irina") != std::string::npos ||
									    vName.find("Pavel") != std::string::npos ||
									    vName.find("Elena") != std::string::npos ||
									    vName.find("Aleksandr") != std::string::npos ||
									    vName.find("Tatyana") != std::string::npos)
									{
										pSelectedToken = pToken;
										LogSapi("[Soundpad SAPI] Auto-selected Russian voice: '%s'\n", vName.c_str());
										CoTaskMemFree(pDesc);
										break;
									}
								}
							}
							CoTaskMemFree(pDesc);
						}
						pToken->Release();
					}
				}

				if (pSelectedToken)
				{
					pVoice->SetVoice(pSelectedToken);
					pSelectedToken->Release();
				}
				else if (hasCyrillic && targetVoice.empty())
				{
					LogSapi("[Soundpad SAPI] Notice: Russian text detected, but no Russian 32-bit voice installed in WOW6432Node. Using system default.\n");
				}

				pEnum->Release();
			}
		}
		pCat->Release();
	}

	// 6. Direct output to memory stream.
	// Try FALSE first (forces SAPI internal format converter to output 22050 Hz Mono PCM), fallback to TRUE.
	hr = pVoice->SetOutput(pSpStream, FALSE);
	if (FAILED(hr))
	{
		hr = pVoice->SetOutput(pSpStream, TRUE);
	}
	if (FAILED(hr))
	{
		DebuLog("SAPI: SetOutput failed hr=0x%08X", hr);
		LogSapi("[Soundpad SAPI] SetOutput failed: hr=0x%08X\n", hr);
		pVoice->Release();
		pSpStream->Release();
		pMemStream->Release();
		return pcm;
	}

	USHORT vol = static_cast<USHORT>(std::clamp(cfg.volume, 0.0f, 1.0f) * 100.0f);
	pVoice->SetVolume(vol);

	long rate = static_cast<long>(std::clamp(cfg.rate, -10.0f, 10.0f));
	pVoice->SetRate(rate);

	// 7. Render speech synchronously into the memory stream.
	// SPF_IS_NOT_XML (0x10) disables XML parsing to prevent <silence> or tag abuse from chat.
	constexpr DWORD kSpfIsNotXml = 0x10;
	hr = pVoice->Speak(wtext.c_str(), kSpfIsNotXml, NULL);
	if (FAILED(hr))
	{
		DebuLog("SAPI: pVoice->Speak failed hr=0x%08X", hr);
		LogSapi("[Soundpad SAPI] pVoice->Speak failed: hr=0x%08X\n", hr);
		pVoice->Release();
		pSpStream->Release();
		pMemStream->Release();
		return pcm;
	}

	// Query actual format in case SAPI altered it
	GUID fmtId = {};
	WAVEFORMATEX* pActualWfx = nullptr;
	if (SUCCEEDED(pSpStream->GetFormat(&fmtId, &pActualWfx)) && pActualWfx)
	{
		if (pActualWfx->nSamplesPerSec > 0)
		{
			outSampleRate = static_cast<int>(pActualWfx->nSamplesPerSec);
		}
		CoTaskMemFree(pActualWfx);
	}

	// 8. Read back rendered audio bytes from memory BEFORE closing stream
	std::vector<uint8_t> rawBytes;

	HGLOBAL hGlobal = NULL;
	if (SUCCEEDED(GetHGlobalFromStream(pMemStream, &hGlobal)) && hGlobal)
	{
		STATSTG stat = {};
		size_t actualSize = 0;
		if (SUCCEEDED(pMemStream->Stat(&stat, STATFLAG_NONAME)) && stat.cbSize.QuadPart > 0)
		{
			actualSize = static_cast<size_t>(stat.cbSize.QuadPart);
		}
		else
		{
			actualSize = GlobalSize(hGlobal);
		}

		void* pLock = GlobalLock(hGlobal);
		if (pLock && actualSize > 0)
		{
			rawBytes.assign(static_cast<const uint8_t*>(pLock), static_cast<const uint8_t*>(pLock) + actualSize);
			GlobalUnlock(hGlobal);
		}
	}

	if (rawBytes.empty())
	{
		STATSTG stat = {};
		if (SUCCEEDED(pMemStream->Stat(&stat, STATFLAG_NONAME)))
		{
			ULONG totalBytes = static_cast<ULONG>(stat.cbSize.QuadPart);
			if (totalBytes > 0)
			{
				rawBytes.resize(totalBytes);
				LARGE_INTEGER liZero = {};
				pMemStream->Seek(liZero, STREAM_SEEK_SET, NULL);
				ULONG bytesRead = 0;
				pMemStream->Read(rawBytes.data(), totalBytes, &bytesRead);
				rawBytes.resize(bytesRead);
			}
		}
	}

	// 9. Clean up COM objects
	pVoice->SetOutput(nullptr, FALSE);
	pVoice->Release();
	pSpStream->Close();
	pSpStream->Release();
	pMemStream->Release();

	// 10. Extract 16-bit PCM samples
	if (rawBytes.size() >= 44 &&
	    rawBytes[0] == 'R' && rawBytes[1] == 'I' && rawBytes[2] == 'F' && rawBytes[3] == 'F')
	{
		size_t offset = 12;
		bool foundData = false;
		while (offset + 8 <= rawBytes.size())
		{
			uint32_t chunkId = 0;
			uint32_t chunkSize = 0;
			memcpy(&chunkId, rawBytes.data() + offset, 4);
			memcpy(&chunkSize, rawBytes.data() + offset + 4, 4);

			if (chunkId == 0x61746164) // "data"
			{
				size_t dataStart = offset + 8;
				size_t validBytes = (std::min)(static_cast<size_t>(chunkSize), rawBytes.size() - dataStart);
				size_t samples = validBytes / sizeof(int16_t);
				pcm.resize(samples);
				memcpy(pcm.data(), rawBytes.data() + dataStart, samples * sizeof(int16_t));
				foundData = true;
				break;
			}
			offset += 8 + ((chunkSize + 1) & ~1);
		}
		if (!foundData)
		{
			size_t validBytes = rawBytes.size() - 44;
			size_t samples = validBytes / sizeof(int16_t);
			pcm.resize(samples);
			memcpy(pcm.data(), rawBytes.data() + 44, samples * sizeof(int16_t));
		}
	}
	else
	{
		size_t samples = rawBytes.size() / sizeof(int16_t);
		if (samples > 0)
		{
			pcm.resize(samples);
			memcpy(pcm.data(), rawBytes.data(), samples * sizeof(int16_t));
		}
	}

	if (!pcm.empty())
	{
		DebuLog("SAPI: Synthesized %zu samples (%.2f sec, %d Hz)",
			pcm.size(), static_cast<float>(pcm.size()) / static_cast<float>(outSampleRate), outSampleRate);
		LogSapi("[Soundpad SAPI] Synthesized %zu samples (%.2f sec, %d Hz).\n",
			pcm.size(), static_cast<float>(pcm.size()) / static_cast<float>(outSampleRate), outSampleRate);
	}
	else
	{
		DebuLog("SAPI: Synthesis returned 0 samples");
		LogSapi("[Soundpad SAPI] Synthesis produced 0 samples.\n");
	}

	return pcm;
}

} // namespace Sapi

#else // Non-Windows stub for compilation/testing

namespace Sapi
{

bool IsAvailable() { return false; }
std::vector<std::string> GetAvailableVoices() { return {}; }
std::vector<int16_t> SynthesizeText(const std::string&, int& outSampleRate, const SapiConfig&)
{
	outSampleRate = 22050;
	return {};
}

} // namespace Sapi

#endif
