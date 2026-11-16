#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace Klatter
{

enum class VoicingMode
{
	Natural = 0,
	Whisper = 1
};

struct KlatterConfig
{
	float baseF0 = 220.0f;       // Base pitch (Hz)
	float speed = 1.0f;          // Speech rate (1.0 = normal)
	float jitterHz = 25.0f;      // R.E.P.O. discrete pitch offset per word
	bool repoJumpingPitch = true;// Enable discrete jumping pitch [Normal, High, Low]
	bool randomStyleAtStart = false; // Random style (Regular, Squeak, Whisper) at start of phrase
	VoicingMode mode = VoicingMode::Natural;
	int sampleRate = 11025;      // 11025 Hz default
};

class BiquadFilter
{
public:
	float b = 0.0f, c = 0.0f, d = 0.0f, e = 0.0f, f = 0.0f;

	void Reset();
	void SetResonance(float freq, float bw, float sr);
	void SetAntiresonance(float freq, float bw, float sr);
	void SetGain(float gain);
	float ProcessResonance(float val);
	float ProcessAntiresonance(float val);
};

class KlatterTranslator
{
public:
	static KlatterTranslator& Get();

	std::string TextToPhonemes(const std::string& text);
	static std::string TranslateSpecialLetters(const std::string& text);

private:
	std::string TranslateWord(const std::string& word);
	bool IsAbbreviation(const std::string& word);
	std::string SpellOut(const std::string& word);

	bool CheckLeft(const char* ruleLeft, const std::string& text, int pos);
	bool CheckRight(const char* ruleRight, const std::string& text, int pos);

	static bool IsVowel(char c);
	static bool IsUpper(char c);
	static bool IsLower(char c);
	static bool IsAlpha(char c);
	static bool IsConsonant(char c);
};

class KlatterEngine
{
public:
	KlatterEngine(int sampleRate = 11025);

	// Synthesize input text into 16-bit mono PCM samples at the configured sampleRate
	std::vector<int16_t> SynthesizeText(const std::string& text, const KlatterConfig& config);

	// Synthesize raw phoneme string into 16-bit mono PCM samples
	std::vector<int16_t> SynthesizePhonemes(const std::string& phStr, const KlatterConfig& config);

private:
	struct InterpParam
	{
		float a = 0.0f;
		int b = 0;
	};

	void ResetState();
	float GetGain(float idx);
	float NaturalSource();
	float WhisperSource();
	void UpdateFlutter(float baseF0);
	void ResetPeriod(float f0, int oq, int oqExtra, float flutterSpeed);
	void ApplyParams(const float* p);
	float GetInterpolatedParam(int idx, float target, int currF, int totalF);

	void SynthesizeWordUnit(const std::string& wordPh, float wordF0, int msPerFrame, std::vector<float>& outAudio);

	int m_sr;
	uint32_t m_ax = 322402983;
	float m_aa = 0.0f;

	int m_j = 0;
	int m_k = 4;
	int m_l = 120;
	int m_mGate = 4;
	float m_qVoice = 0.0f;
	float m_uNoise = 0.0f;
	float m_w = 0.0f;
	float m_x = 0.0f;
	float m_y = 0.0f;
	float m_z = 0.0f;
	float m_ac = 0.0f;
	float m_ad = 1.0f;
	float m_abDiff = 0.0f;

	BiquadFilter m_an;
	BiquadFilter m_ak;
	BiquadFilter m_al;
	BiquadFilter m_ae;
	BiquadFilter m_af;
	BiquadFilter m_ag;
	BiquadFilter m_ah;
	BiquadFilter m_ai;
	BiquadFilter m_aj;
	BiquadFilter m_ao;

	float m_rBypass = 0.0f;
	float m_sFric = 0.0f;
	float m_tAsp = 0.0f;
	VoicingMode m_voiceSource = VoicingMode::Natural;

	float m_flutterAmount = 10.0f;
	float m_flutterSpeed = 1.0f;
	int m_f0Flutter = 0;
	int m_continuousFrameIdx = 0;

	InterpParam m_transIn[19];
	InterpParam m_transOut[19];

	float m_f1Freq = 500;
	float m_f2Freq = 1500;
	float m_f3Freq = 2800;
	float m_f4Freq = 3250;
	float m_f5Freq = 3700;
	float m_f6Freq = 4990;
	float m_b1Bw = 80;
	float m_b2Bw = 200;
	float m_b3Bw = 350;
	float m_b4Bw = 500;
	float m_b5Bw = 600;
	float m_b6Bw = 800;
	float m_fnFreq = 270;
	float m_fnBw = 100;
	float m_fpFreq = 270;
	float m_fpBw = 100;
	float m_avVal = 0;
	float m_baseF0 = 220.0f;
	float m_preEmphasisPrev = 0.0f;
};

} // namespace Klatter
