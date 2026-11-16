#include "KlatterEngine.h"
#include "KlatterData.h"

#include <cmath>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <random>

namespace Klatter
{

static const float PI = 3.14159265358979323846f;

// -------------------------------------------------------------------------------------------------
// BiquadFilter Implementation
// -------------------------------------------------------------------------------------------------

void BiquadFilter::Reset()
{
	b = c = d = e = f = 0.0f;
}

void BiquadFilter::SetResonance(float freq, float bw, float sr)
{
	if (freq < 0.01f) freq = 0.01f;
	if (bw < 0.01f) bw = 0.01f;
	float num = -PI / sr * bw;
	float num2 = std::exp(num);
	d = -num2 * num2;
	num = -2.0f * PI / sr * freq;
	c = num2 * std::cos(num) * 2.0f;
	b = 1.0f - c - d;
}

void BiquadFilter::SetAntiresonance(float freq, float bw, float sr)
{
	SetResonance(freq, bw, sr);
	if (std::abs(b) < 1e-12f) b = 1e-12f;
	b = 1.0f / b;
	c *= -b;
	d *= -b;
}

void BiquadFilter::SetGain(float gain)
{
	b *= gain;
}

float BiquadFilter::ProcessResonance(float val)
{
	float res = b * val + c * e + d * f;
	f = e;
	e = res;
	return res;
}

float BiquadFilter::ProcessAntiresonance(float val)
{
	float res = b * val + c * e + d * f;
	f = e;
	e = val;
	return res;
}

// -------------------------------------------------------------------------------------------------
// KlatterTranslator Implementation (NRL English Rules)
// -------------------------------------------------------------------------------------------------

KlatterTranslator& KlatterTranslator::Get()
{
	static KlatterTranslator instance;
	return instance;
}

bool KlatterTranslator::IsVowel(char c)
{
	return c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U' ||
	       c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

bool KlatterTranslator::IsUpper(char c)
{
	return c >= 'A' && c <= 'Z';
}

bool KlatterTranslator::IsLower(char c)
{
	return c >= 'a' && c <= 'z';
}

bool KlatterTranslator::IsAlpha(char c)
{
	return IsUpper(c) || IsLower(c);
}

bool KlatterTranslator::IsConsonant(char c)
{
	return IsUpper(c) && !IsVowel(c);
}

bool KlatterTranslator::CheckLeft(const char* ruleLeft, const std::string& text, int pos)
{
	if (!ruleLeft || !*ruleLeft) return true;
	int len = static_cast<int>(std::strlen(ruleLeft));
	pos -= 1;
	for (int i = len - 1; i >= 0; --i)
	{
		char c = ruleLeft[i];
		if (pos < 0) return false;
		if (IsAlpha(c) || c == '\'' || c == ' ')
		{
			if (c != text[pos]) return false;
			pos--;
		}
		else if (c == '#')
		{
			if (!IsVowel(text[pos])) return false;
			pos--;
			while (pos >= 0 && IsVowel(text[pos])) pos--;
		}
		else if (c == ':')
		{
			while (pos >= 0 && IsConsonant(text[pos])) pos--;
		}
		else if (c == '^')
		{
			if (!IsConsonant(text[pos])) return false;
			pos--;
		}
		else if (c == '.')
		{
			if (std::strchr("BDVGJLMNRWZ", text[pos]) == nullptr) return false;
			pos--;
		}
		else if (c == '+')
		{
			if (std::strchr("EIY", text[pos]) == nullptr) return false;
			pos--;
		}
		else
		{
			return false;
		}
	}
	return true;
}

bool KlatterTranslator::CheckRight(const char* ruleRight, const std::string& text, int pos)
{
	if (!ruleRight || !*ruleRight) return true;
	int tLen = static_cast<int>(text.length());
	for (const char* p = ruleRight; *p; ++p)
	{
		char c = *p;
		if (pos >= tLen) return false;
		if (IsAlpha(c) || c == '\'' || c == ' ')
		{
			if (c != text[pos]) return false;
			pos++;
		}
		else if (c == '#')
		{
			if (!IsVowel(text[pos])) return false;
			pos++;
			while (pos < tLen && IsVowel(text[pos])) pos++;
		}
		else if (c == ':')
		{
			while (pos < tLen && IsConsonant(text[pos])) pos++;
		}
		else if (c == '^')
		{
			if (!IsConsonant(text[pos])) return false;
			pos++;
		}
		else if (c == '.')
		{
			if (std::strchr("BDVGJLMNRWZ", text[pos]) == nullptr) return false;
			pos++;
		}
		else if (c == '+')
		{
			if (std::strchr("EIY", text[pos]) == nullptr) return false;
			pos++;
		}
		else if (c == '%')
		{
			if (text[pos] == 'E')
			{
				pos++;
				if (pos < tLen && text[pos] == 'L')
				{
					pos++;
					if (pos < tLen && text[pos] == 'Y') pos++;
				}
				else if (pos < tLen && std::strchr("RSD", text[pos]) != nullptr)
				{
					pos++;
				}
			}
			else if (text[pos] == 'I')
			{
				pos++;
				if (pos < tLen && text[pos] == 'N')
				{
					pos++;
					if (pos < tLen && text[pos] == 'G')
						pos++;
					else return false;
				}
				else return false;
			}
			else return false;
		}
		else
		{
			return false;
		}
	}
	return true;
}

static const char* const kLetterNames[26] = {
	"eI", "bi", "si", "di", "i", "ef", "dZi", "eItS", "aI", "dZeI",
	"keI", "el", "em", "en", "@U", "pi", "kju", "ar", "es", "ti",
	"ju", "vi", "dVblju", "eks", "waI", "zi"
};

static const char* const kDigitNames[10] = {
	"zI@r@U", "wVn", "tu", "Tri", "fOr", "faIv", "sIks", "sev@n", "eIt", "naIn"
};

std::string KlatterTranslator::TranslateWord(const std::string& word)
{
	std::string upperWord;
	for (char c : word)
		upperWord += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

	std::string text = " " + upperWord + " ";
	std::string phonemes;
	int pos = 1;
	int textLen = static_cast<int>(text.length());

	while (pos < textLen - 1)
	{
		char c = text[pos];
		int charIdx = (c >= 'A' && c <= 'Z') ? (c - 'A' + 1) : 0;

		const TranslationRule* rules = g_RulesIndex[charIdx];
		size_t ruleCount = g_RulesCount[charIdx];
		bool found = false;

		for (size_t r = 0; r < ruleCount; ++r)
		{
			const auto& rule = rules[r];
			size_t bodyLen = std::strlen(rule.body);
			if (text.compare(pos, bodyLen, rule.body) == 0)
			{
				if (CheckLeft(rule.left, text, pos) && CheckRight(rule.right, text, pos + static_cast<int>(bodyLen)))
				{
					phonemes += rule.phonemes;
					int advance = (bodyLen > 0) ? static_cast<int>(bodyLen) : 1;
					if (pos <= textLen - advance)
						pos += advance;
					else
						pos = textLen;
					found = true;
					break;
				}
			}
		}

		if (!found)
		{
			// Fallback: spell out unhandled characters (digits and isolated letters) instead of silent drop
			if (c >= '0' && c <= '9')
			{
				phonemes += kDigitNames[c - '0'];
			}
			else if (c >= 'A' && c <= 'Z')
			{
				phonemes += kLetterNames[c - 'A'];
			}
			pos++;
		}
	}

	return phonemes;
}

bool KlatterTranslator::IsAbbreviation(const std::string& word)
{
	if (word.empty()) return false;
	if (word.length() == 1) return true;

	bool hasVowel = false;
	for (char c : word)
	{
		if (IsVowel(c))
		{
			hasVowel = true;
			break;
		}
	}
	if (!hasVowel) return true;

	// All upper and <= 4 chars
	bool allUpper = true;
	for (char c : word)
	{
		if (!IsUpper(c)) { allUpper = false; break; }
	}
	if (allUpper && word.length() <= 4)
		return true;

	return false;
}

std::string KlatterTranslator::SpellOut(const std::string& word)
{
	std::string res;
	for (char c : word)
	{
		char u = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
		if (u >= 'A' && u <= 'Z')
		{
			if (!res.empty()) res += " ";
			res += kLetterNames[u - 'A'];
		}
		else if (u >= '0' && u <= '9')
		{
			if (!res.empty()) res += " ";
			res += kDigitNames[u - '0'];
		}
	}
	return res;
}

std::string KlatterTranslator::TranslateSpecialLetters(const std::string& text)
{
	std::string res;
	res.reserve(text.length() * 2);

	for (size_t i = 0; i < text.length(); )
	{
		unsigned char c = static_cast<unsigned char>(text[i]);

		// UTF-8 German Umlauts
		if (c == 0xC3 && i + 1 < text.length())
		{
			unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
			if (c2 == 0xB6 || c2 == 0x96) { res += "oe"; i += 2; continue; } // ö / Ö
			if (c2 == 0xA4 || c2 == 0x84) { res += "ae"; i += 2; continue; } // ä / Ä
			if (c2 == 0xBC || c2 == 0x9C) { res += "ue"; i += 2; continue; } // ü / Ü
			if (c2 == 0x9F) { res += "ss"; i += 2; continue; } // ß
		}

		// UTF-8 2-byte Cyrillic: 0xD0, 0xD1, 0xD2
		if ((c == 0xD0 || c == 0xD1 || c == 0xD2) && (i + 1 < text.length()))
		{
			unsigned char c2 = static_cast<unsigned char>(text[i + 1]);
			uint16_t code = (static_cast<uint16_t>(c) << 8) | c2;
			i += 2;

			switch (code)
			{
			case 0xD090: res += "A"; break; // А
			case 0xD091: res += "B"; break; // Б
			case 0xD092: res += "V"; break; // В
			case 0xD093: res += "G"; break; // Г
			case 0xD094: res += "D"; break; // Д
			case 0xD095: res += "E"; break; // Е
			case 0xD081: res += "Yo"; break; // Ё
			case 0xD096: res += "Zh"; break; // Ж
			case 0xD097: res += "Z"; break; // З
			case 0xD098: res += "I"; break; // И
			case 0xD099: res += "Y"; break; // Й
			case 0xD09A: res += "K"; break; // К
			case 0xD09B: res += "L"; break; // Л
			case 0xD09C: res += "M"; break; // М
			case 0xD09D: res += "N"; break; // Н
			case 0xD09E: res += "O"; break; // О
			case 0xD09F: res += "P"; break; // П
			case 0xD0A0: res += "R"; break; // Р
			case 0xD0A1: res += "S"; break; // С
			case 0xD0A2: res += "T"; break; // Т
			case 0xD0A3: res += "U"; break; // У
			case 0xD0A4: res += "F"; break; // Ф
			case 0xD0A5: res += "Kh"; break; // Х
			case 0xD0A6: res += "Ts"; break; // Ц
			case 0xD0A7: res += "Ch"; break; // Ч
			case 0xD0A8: res += "Sh"; break; // Ш
			case 0xD0A9: res += "Shch"; break; // Щ
			case 0xD0AA: break; // Ъ
			case 0xD0AB: res += "Y"; break; // Ы
			case 0xD0AC: break; // Ь
			case 0xD0AD: res += "E"; break; // Э
			case 0xD0AE: res += "Yu"; break; // Ю
			case 0xD0AF: res += "Ya"; break; // Я

			case 0xD0B0: res += "a"; break; // а
			case 0xD0B1: res += "b"; break; // б
			case 0xD0B2: res += "v"; break; // в
			case 0xD0B3: res += "g"; break; // г
			case 0xD0B4: res += "d"; break; // д
			case 0xD0B5: res += "e"; break; // е
			case 0xD191: res += "yo"; break; // ё
			case 0xD0B6: res += "zh"; break; // ж
			case 0xD0B7: res += "z"; break; // з
			case 0xD0B8: res += "i"; break; // и
			case 0xD0B9: res += "y"; break; // й
			case 0xD0BA: res += "k"; break; // к
			case 0xD0BB: res += "l"; break; // л
			case 0xD0BC: res += "m"; break; // м
			case 0xD0BD: res += "n"; break; // н
			case 0xD0BE: res += "o"; break; // о
			case 0xD0BF: res += "p"; break; // п
			case 0xD180: res += "r"; break; // р
			case 0xD181: res += "s"; break; // с
			case 0xD182: res += "t"; break; // т
			case 0xD183: res += "u"; break; // у
			case 0xD184: res += "f"; break; // ф
			case 0xD185: res += "kh"; break; // х
			case 0xD186: res += "ts"; break; // ц
			case 0xD187: res += "ch"; break; // ч
			case 0xD188: res += "sh"; break; // ш
			case 0xD189: res += "shch"; break; // щ
			case 0xD18A: break; // ъ
			case 0xD18B: res += "y"; break; // ы
			case 0xD18C: break; // ь
			case 0xD18D: res += "e"; break; // э
			case 0xD18E: res += "yu"; break; // ю
			case 0xD18F: res += "ya"; break; // я

			// Ukrainian additions
			case 0xD086: res += "I"; break;
			case 0xD196: res += "i"; break;
			case 0xD087: res += "Yi"; break;
			case 0xD197: res += "yi"; break;
			case 0xD084: res += "Ye"; break;
			case 0xD194: res += "ye"; break;
			case 0xD290: res += "G"; break;
			case 0xD291: res += "g"; break;

			default:
				break;
			}
			continue;
		}

		// CP1251 single-byte Cyrillic
		if (c >= 0xC0)
		{
			static const char* const kCp1251Upper[] = {
				"A", "B", "V", "G", "D", "E", "Zh", "Z", "I", "Y", "K", "L", "M", "N", "O", "P",
				"R", "S", "T", "U", "F", "Kh", "Ts", "Ch", "Sh", "Shch", "", "Y", "", "E", "Yu", "Ya"
			};
			static const char* const kCp1251Lower[] = {
				"a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
				"r", "s", "t", "u", "f", "kh", "ts", "ch", "sh", "shch", "", "y", "", "e", "yu", "ya"
			};

			if (c <= 0xDF)
				res += kCp1251Upper[c - 0xC0];
			else
				res += kCp1251Lower[c - 0xE0];

			++i;
			continue;
		}
		if (c == 0xA8) { res += "Yo"; ++i; continue; } // Ё
		if (c == 0xB8) { res += "yo"; ++i; continue; } // ё

		res += text[i++];
	}

	return res;
}

static std::string NumberToWords(long long n)
{
	static const char* const kUnder20[] = {
		"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
		"ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"
	};
	static const char* const kTens[] = {
		"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
	};

	if (n < 0)
		return "minus " + NumberToWords(-n);
	if (n < 20)
		return kUnder20[n];
	if (n < 100)
	{
		std::string s = kTens[n / 10];
		if (n % 10) s += " " + std::string(kUnder20[n % 10]);
		return s;
	}
	if (n < 1000)
	{
		std::string s = std::string(kUnder20[n / 100]) + " hundred";
		if (n % 100) s += " " + NumberToWords(n % 100);
		return s;
	}
	if (n < 1000000)
	{
		std::string s = NumberToWords(n / 1000) + " thousand";
		if (n % 1000) s += " " + NumberToWords(n % 1000);
		return s;
	}
	return "many";
}

static std::string ExpandNumbersInText(const std::string& input)
{
	std::string output;
	output.reserve(input.size() * 2);
	for (size_t i = 0; i < input.size(); )
	{
		if (std::isdigit(static_cast<unsigned char>(input[i])))
		{
			size_t j = i;
			while (j < input.size() && std::isdigit(static_cast<unsigned char>(input[j])))
				++j;
			std::string numStr = input.substr(i, j - i);
			long long val = 0;
			try { val = std::stoll(numStr); } catch (...) { val = 0; }
			if (!output.empty() && output.back() != ' ')
				output += ' ';
			output += NumberToWords(val);
			if (j < input.size() && input[j] != ' ' && !std::ispunct(static_cast<unsigned char>(input[j])))
				output += ' ';
			i = j;
		}
		else
		{
			output += input[i++];
		}
	}
	return output;
}

std::string KlatterTranslator::TextToPhonemes(const std::string& text)
{
	std::string norm = TranslateSpecialLetters(text);
	norm = ExpandNumbersInText(norm);
	std::string result;
	std::string curWord;

	for (size_t i = 0; i < norm.length(); ++i)
	{
		char c = norm[i];
		if (IsAlpha(c) || c == '\'')
		{
			curWord += c;
		}
		else
		{
			if (!curWord.empty())
			{
				if (IsAbbreviation(curWord))
				{
					if (!result.empty() && result.back() != ' ') result += ' ';
					result += SpellOut(curWord);
				}
				else
				{
					if (!result.empty() && result.back() != ' ') result += ' ';
					result += TranslateWord(curWord);
				}
				curWord.clear();
			}

			if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == '.' || c == '!' || c == '?' || c == ';')
			{
				if (!result.empty() && result.back() != ' ')
					result += ' ';
			}
		}
	}

	if (!curWord.empty())
	{
		if (IsAbbreviation(curWord))
		{
			if (!result.empty() && result.back() != ' ') result += ' ';
			result += SpellOut(curWord);
		}
		else
		{
			if (!result.empty() && result.back() != ' ') result += ' ';
			result += TranslateWord(curWord);
		}
	}

	return result;
}

// -------------------------------------------------------------------------------------------------
// KlatterEngine Implementation (Formant Synthesizer)
// -------------------------------------------------------------------------------------------------

KlatterEngine::KlatterEngine(int sampleRate)
	: m_sr(sampleRate)
{
	ResetState();
}

void KlatterEngine::ResetState()
{
	m_j = 0;
	m_k = 4;
	m_l = 120;
	m_mGate = 4;
	m_qVoice = 0.0f;
	m_uNoise = 0.0f;
	m_w = m_x = m_y = m_z = 0.0f;
	m_ac = 0.0f;
	m_ad = 1.0f;
	m_abDiff = 0.0f;
	m_aa = 0.0f;
	m_continuousFrameIdx = 0;

	m_an.Reset();
	m_ak.Reset();
	m_al.Reset();
	m_ae.Reset();
	m_af.Reset();
	m_ag.Reset();
	m_ah.Reset();
	m_ai.Reset();
	m_aj.Reset();
	m_ao.Reset();
	m_preEmphasisPrev = 0.0f;

	for (auto& item : m_transIn) item = InterpParam{};
	for (auto& item : m_transOut) item = InterpParam{};
}

float KlatterEngine::GetGain(float idx)
{
	int i = static_cast<int>(idx);
	if (i < 0) i = 0;
	if (i > 87) i = 87;
	return g_GainTable[i];
}

float KlatterEngine::NaturalSource()
{
	if (m_j < m_l)
	{
		m_w -= m_x;
		m_y += m_w;
		return m_y * 0.028f;
	}
	m_y = 0.0f;
	return 0.0f;
}

float KlatterEngine::WhisperSource()
{
	return std::fmod(m_aa, 4000.0f);
}

void KlatterEngine::UpdateFlutter(float baseF0)
{
	if (baseF0 < 1.0f) return;
	float num2 = baseF0 * 0.001f;
	float num3 = 2.0f * PI * m_flutterSpeed * (m_continuousFrameIdx / 100.0f);
	float num4 = std::sin(1.27f * num3);
	float num5 = std::sin(0.71f * num3);
	float num6 = std::sin(0.47f * num3);
	m_f0Flutter = static_cast<int>(m_flutterAmount * num2 * (num4 + num5 + num6));
}

void KlatterEngine::ResetPeriod(float f0, int oq, int oqExtra, float flutterSpeed)
{
	float actualF0 = f0 + static_cast<float>(m_f0Flutter);
	if (actualF0 > 0.0f)
	{
		// Glottal pulse generator advances 4 times per output sample (sub < 4).
		// Accurate period in 4x sub-samples is (4.0f * m_sr) / actualF0.
		m_k = static_cast<int>(4.0f * m_sr / actualF0);
		if (m_k < 8) m_k = 8;
		m_qVoice = GetGain(m_avVal - 7.0f);
		m_mGate = m_k;
		if (m_avVal > 0.0f)
			m_mGate >>= 1;

		m_uNoise = GetGain(static_cast<float>(oqExtra)) * 0.1f;

		// Preserve phoneme open quotient articulation; fallback to 30 if negative/unspecified
		int effectiveOq = (oq > 0) ? oq : 30;
		m_l = 4 * effectiveOq;
		if (m_l >= m_k - 1) m_l = m_k - 2;
		if (m_l < 4) m_l = 4;

		m_x = 1920000.0f / (static_cast<float>(m_l) * static_cast<float>(m_l));
		m_w = m_x * static_cast<float>(m_l) * 0.333f;

		float aLow = static_cast<float>(m_sr / m_l);
		m_an.SetResonance(0.0f, aLow, static_cast<float>(m_sr));
		float numGlot = static_cast<float>(m_l) * 0.00833f;
		m_an.SetGain(numGlot * numGlot);
	}
	else
	{
		m_k = 110250;
		m_qVoice = GetGain(m_avVal - 7.0f);
		m_mGate = m_k;
		m_uNoise = GetGain(static_cast<float>(oqExtra)) * 0.1f;
		m_w = 0.0f;
		m_x = 0.0f;
	}

	if (m_k != 4 || m_j == 0)
	{
		m_ac = 0.033f * flutterSpeed;
		m_ad = (m_ac > 0.0f) ? (1.0f - m_ac) : 1.0f;
	}
}

void KlatterEngine::ApplyParams(const float* p)
{
	float f0 = p[19];
	m_avVal = p[15];

	UpdateFlutter(f0);
	ResetPeriod(f0, static_cast<int>(p[7]), static_cast<int>(p[16]), 10.0f);

	m_sFric = GetGain(p[17]) * 0.05f;
	m_tAsp = GetGain(p[18]) * 0.25f;
	m_rBypass = GetGain(p[14] + 24.0f) * 0.05f;

	float srF = static_cast<float>(m_sr);
	m_ak.SetResonance(m_fpFreq, m_fpBw, srF);
	m_al.SetAntiresonance(p[0], m_fnBw, srF);

	m_ae.SetResonance(p[1], p[4], srF);
	m_ae.SetGain(GetGain(p[8] + 24.0f) * 0.4f);

	m_af.SetResonance(p[2], p[5], srF);
	m_af.SetGain(GetGain(p[9] + 24.0f) * 0.15f);

	m_ag.SetResonance(p[3], p[6], srF);
	m_ag.SetGain(GetGain(p[10] + 24.0f) * 0.06f);

	m_ah.SetResonance(m_f4Freq, m_b4Bw, srF);
	m_ah.SetGain(GetGain(p[11] + 24.0f) * 0.04f);

	m_ai.SetResonance(m_f5Freq, m_b5Bw, srF);
	m_ai.SetGain(GetGain(p[12] + 24.0f) * 0.022f);

	m_aj.SetResonance(m_f6Freq, m_b6Bw, srF);
	m_aj.SetGain(GetGain(p[13] + 24.0f) * 0.03f);

	m_ao.SetResonance(10.0f, srF, srF);
	m_ao.SetGain(GetGain(58.0f));
}

[[maybe_unused]] static float InterpolateLin(float start, float end, float curr, float total)
{
	if (total <= 0.0f) return start;
	if (curr >= total) return end;
	return start + (end - start) * (curr / total);
}

static float InterpolateParam(int idx, float start, float end, float curr, float total)
{
	if (total <= 0.0f) return start;
	if (curr >= total) return end;
	float t = curr / total;

	// Logarithmic / geometric interpolation for frequency resonances (FN, F1, F2, F3)
	// Acoustic perception of formants is logarithmic; linear frequency transitions distort vowels
	if (idx >= 0 && idx <= 3)
	{
		if (start > 30.0f && end > 30.0f)
		{
			return std::exp(std::log(start) + t * (std::log(end) - std::log(start)));
		}
	}

	return start + (end - start) * t;
}

float KlatterEngine::GetInterpolatedParam(int idx, float target, int currF, int totalF)
{
	if (idx < 0 || idx >= 19)
		return target;

	const auto& tin = m_transIn[idx];
	const auto& tout = m_transOut[idx];
	int num = totalF - (tin.b + tout.b);

	if (num >= 0)
	{
		if (currF < tin.b)
			return InterpolateParam(idx, tin.a, target, static_cast<float>(currF), static_cast<float>(tin.b));
		currF -= tin.b;
		if (currF <= num)
			return target;
		return InterpolateParam(idx, target, tout.a, static_cast<float>(currF - num), static_cast<float>(tout.b));
	}
	else
	{
		float alpha = static_cast<float>(currF) / static_cast<float>(std::max(1, totalF));
		float vIn = InterpolateParam(idx, tin.a, target, static_cast<float>(currF), static_cast<float>(tin.b));
		float vOut = InterpolateParam(idx, tout.a, target, static_cast<float>(totalF - currF), static_cast<float>(tout.b));
		if (idx >= 0 && idx <= 3 && vIn > 30.0f && vOut > 30.0f)
		{
			return std::exp((1.0f - alpha) * std::log(vIn) + alpha * std::log(vOut));
		}
		return (1.0f - alpha) * vIn + alpha * vOut;
	}
}

void KlatterEngine::SynthesizeWordUnit(const std::string& wordPh, float wordF0, int msPerFrame, std::vector<float>& outAudio)
{
	if (wordPh.empty())
		return;

	m_j = 0;
	m_z = 0.0f;
	m_aa = 0.0f;
	m_abDiff = 0.0f;

	struct Triplet
	{
		int idx;
		int dur;
		int stress;

		Triplet(int i, int d, int s) : idx(i), dur(d), stress(s) {}
	};

	std::vector<Triplet> triplets;
	int i = 0;
	int currentStress = 0;
	int phLen = static_cast<int>(wordPh.length());

	while (i < phLen)
	{
		char c1 = wordPh[i];
		if (c1 == '\'') { currentStress = 3; i++; continue; }
		else if (c1 == ',') { currentStress = 2; i++; continue; }
		else if (c1 == '+') { currentStress = 1; i++; continue; }

		const PhonemeMapping* mapping = nullptr;
		char c2 = (i + 1 < phLen) ? wordPh[i + 1] : 0;
		uint32_t code2 = static_cast<uint8_t>(c1) | (static_cast<uint8_t>(c2) << 8);

		for (size_t m = 0; m < g_NumPhonemeMappings; ++m)
		{
			if (g_PhonemeMappings[m].code == code2)
			{
				mapping = &g_PhonemeMappings[m];
				i += 2;
				break;
			}
		}

		if (!mapping)
		{
			for (size_t m = 0; m < g_NumPhonemeMappings; ++m)
			{
				if (g_PhonemeMappings[m].code == static_cast<uint32_t>(static_cast<uint8_t>(c1)))
				{
					mapping = &g_PhonemeMappings[m];
					i += 1;
					break;
				}
			}
		}

		if (mapping)
		{
			for (int k = 0; k < mapping->count; ++k)
			{
				int pIdx = mapping->indices[k];
				if (pIdx < static_cast<int>(g_NumPhonemeData))
				{
					const auto& pInfo = g_PhonemeData[pIdx];
					int dur = (pInfo.t1 + pInfo.t2) / 2;
					if (dur < 1) dur = 1;
					if (currentStress == 3) dur = static_cast<int>(dur * 1.5f);
					else if (currentStress == 2) dur = static_cast<int>(dur * 1.2f);
					triplets.emplace_back(pIdx, dur, currentStress);
				}
			}
			currentStress = 0;
		}
		else
		{
			i++;
		}
	}

	if (triplets.empty())
		return;

	triplets.emplace_back(1, 4, 0); // Word-end silence padding

	float currentDeclF0 = wordF0 * 1.15f;
	int prevIdx = 0;

	auto setupTrans = [](InterpParam* array, const PhonemeInfo& fTarget, const PhonemeInfo& fSource, bool isTin) {
		for (int j = 0; j < 19; ++j)
		{
			const auto& pTarget = fTarget.params[j];
			const auto& pSource = fSource.params[j];
			array[j].b = isTin ? pTarget.d : pTarget.e;
			if (array[j].b != 0)
			{
				array[j].a = pTarget.b + static_cast<float>(pTarget.c) * pSource.a * 0.01f;
			}
			else
			{
				array[j].a = pSource.a;
			}
		}
	};

	float sampleAccum = 0.0f;
	float exactSamplesPerFrame = static_cast<float>(m_sr) * static_cast<float>(msPerFrame) / 1000.0f;

	for (size_t t = 0; t < triplets.size(); ++t)
	{
		int currIdx = triplets[t].idx;
		int nextIdx = (t + 1 < triplets.size()) ? triplets[t + 1].idx : 0;

		const auto& fCurr = g_PhonemeData[currIdx];
		const auto& fPrev = g_PhonemeData[prevIdx];
		const auto& fNext = g_PhonemeData[nextIdx];

		if (fCurr.dur > fPrev.dur)
			setupTrans(m_transIn, fCurr, fPrev, false);
		else
			setupTrans(m_transIn, fPrev, fCurr, true);

		if (fNext.dur > fCurr.dur)
			setupTrans(m_transOut, fNext, fCurr, true);
		else
			setupTrans(m_transOut, fCurr, fNext, false);

		int durFrames = triplets[t].dur;
		float targetP[19];
		for (int j = 0; j < 19; ++j)
			targetP[j] = fCurr.params[j].a;

		for (int f = 0; f < durFrames; ++f)
		{
			float currentP[20] = {};
			for (int j = 0; j < 19; ++j)
				currentP[j] = GetInterpolatedParam(j, targetP[j], f, durFrames);

			currentP[19] = currentDeclF0;
			ApplyParams(currentP);

			sampleAccum += exactSamplesPerFrame;
			int frameSamples = static_cast<int>(sampleAccum);
			sampleAccum -= static_cast<float>(frameSamples);

			for (int s = 0; s < frameSamples; ++s)
			{
				// PRNG Noise
				m_ax = (m_ax * 1664525u + 1u);
				int32_t rawN = static_cast<int32_t>(m_ax >> 18);
				if (rawN >= 0x2000) rawN -= 0x4000;

				m_aa = static_cast<float>(rawN) + 0.75f * m_aa;

				float noiseVal = m_aa;
				if (m_j > m_mGate)
					noiseVal *= 0.5f;

				float aspSig = m_tAsp * noiseVal;

				float glotPulse = 0.0f;
				for (int sub = 0; sub < 4; ++sub)
				{
					if (m_voiceSource == VoicingMode::Natural)
						glotPulse = NaturalSource();
					else
						glotPulse = WhisperSource();

					if (m_j >= m_k)
					{
						m_j = 0;
						ResetPeriod(currentP[19], static_cast<int>(currentP[7]), static_cast<int>(currentP[16]), 10.0f);
					}

					glotPulse = m_an.ProcessResonance(glotPulse);
					m_j++;
				}

				glotPulse = m_z = glotPulse * m_ad + m_z * m_ac;
				if (m_j < m_l)
				{
					glotPulse += m_uNoise * static_cast<float>(rawN);
				}

				float voiceSig = m_qVoice * glotPulse;
				float fricSig = m_sFric * noiseVal;

				float srcCombined = voiceSig + fricSig;
				float nasalPath = m_ak.ProcessResonance(m_al.ProcessAntiresonance(srcCombined));

				float f1Out = m_ae.ProcessResonance(nasalPath);

				float excit = aspSig + (nasalPath - m_abDiff);
				m_abDiff = nasalPath;

				float res = f1Out;
				res = m_aj.ProcessResonance(excit) - res;
				res = m_ai.ProcessResonance(excit) - res;
				res = m_ah.ProcessResonance(excit) - res;
				res = m_ag.ProcessResonance(excit) - res;
				res = m_af.ProcessResonance(excit) - res;

				float finalSig = (m_rBypass * excit) - res;
				float out = m_ao.ProcessResonance(finalSig);

				// Pre-emphasis filter for high-frequency formant/consonant intelligibility
				float preEmphasisOut = out - 0.65f * m_preEmphasisPrev;
				m_preEmphasisPrev = out;

				// Store pure waveform with full floating-point headroom (no premature hard-clipping)
				outAudio.push_back(preEmphasisOut / 32768.0f);
			}

			m_continuousFrameIdx++;
		}

		currentDeclF0 -= 0.5f * static_cast<float>(durFrames);
		if (currentDeclF0 < wordF0 * 0.8f)
			currentDeclF0 = wordF0 * 0.8f;

		prevIdx = currIdx;
	}

	// 20ms pause between words
	int pauseSamples = m_sr * 20 / 1000;
	outAudio.insert(outAudio.end(), pauseSamples, 0.0f);
}

static std::vector<int16_t> ConvertFloatToPCMWithLimiter(const std::vector<float>& audioFloat, float targetPeak = 0.85f)
{
	if (audioFloat.empty())
		return {};

	// Find absolute maximum peak
	float maxVal = 0.0f;
	for (float s : audioFloat)
	{
		float a = std::abs(s);
		if (a > maxVal)
			maxVal = a;
	}

	float gain = 1.0f;
	if (maxVal > 0.0001f)
	{
		// Normalize to target peak (capped to prevent boosting pure noise or silence)
		gain = targetPeak / maxVal;
		if (gain > 8.0f)
			gain = 8.0f;
	}

	std::vector<int16_t> pcm(audioFloat.size());
	for (size_t i = 0; i < audioFloat.size(); ++i)
	{
		float val = audioFloat[i] * gain;

		// Soft limiter: gentle hyperbolic tangent saturation above 0.90 to eliminate harsh distortion
		if (val > 0.90f)
		{
			val = 0.90f + 0.10f * std::tanh((val - 0.90f) / 0.10f);
		}
		else if (val < -0.90f)
		{
			val = -0.90f + 0.10f * std::tanh((val + 0.90f) / 0.10f);
		}

		float s = val * 32767.0f;
		if (s < -32768.0f) s = -32768.0f;
		if (s > 32767.0f) s = 32767.0f;
		pcm[i] = static_cast<int16_t>(s);
	}
	return pcm;
}

std::vector<int16_t> KlatterEngine::SynthesizePhonemes(const std::string& phStr, const KlatterConfig& config)
{
	ResetState();
	m_voiceSource = config.mode;
	m_baseF0 = config.baseF0;

	int msPerFrame = static_cast<int>(10.0f / (config.speed > 0.05f ? config.speed : 1.0f));
	if (msPerFrame < 1) msPerFrame = 1;

	std::vector<float> audioFloat;
	SynthesizeWordUnit(phStr, config.baseF0, msPerFrame, audioFloat);

	return ConvertFloatToPCMWithLimiter(audioFloat, 0.85f);
}

std::vector<int16_t> KlatterEngine::SynthesizeText(const std::string& text, const KlatterConfig& config)
{
	ResetState();
	m_voiceSource = config.mode;
	m_baseF0 = config.baseF0;

	int msPerFrame = static_cast<int>(10.0f / (config.speed > 0.05f ? config.speed : 1.0f));
	if (msPerFrame < 1) msPerFrame = 1;

	std::vector<float> audioFloat;

	std::string translitText = KlatterTranslator::TranslateSpecialLetters(text);

	// Split words
	std::vector<std::string> words;
	std::string curWord;
	for (char c : translitText)
	{
		if (std::isspace(static_cast<unsigned char>(c)))
		{
			if (!curWord.empty()) { words.push_back(curWord); curWord.clear(); }
		}
		else
		{
			curWord += c;
		}
	}
	if (!curWord.empty())
		words.push_back(curWord);

	if (words.empty())
		return {};

	std::random_device rd;
	std::mt19937 rng(rd());

	VoicingMode activeMode = config.mode;
	float baseF0 = config.baseF0;

	if (config.randomStyleAtStart)
	{
		// 3 distinct styles within reasonable bounds:
		// 0: Regular natural voice with random base pitch (~170..245 Hz)
		// 1: Squeak (high-pitched comical voice ~320..440 Hz)
		// 2: Whisper (unvoiced rasping whisper, VoicingMode::Whisper)
		int roll = static_cast<int>(rng() % 100);
		if (roll < 52)
		{
			activeMode = VoicingMode::Natural;
			baseF0 = 175.0f + static_cast<float>(rng() % 70); // 175..245 Hz
		}
		else if (roll < 80)
		{
			activeMode = VoicingMode::Natural;
			baseF0 = 320.0f + static_cast<float>(rng() % 120); // 320..440 Hz (squeak)
		}
		else
		{
			activeMode = VoicingMode::Whisper;
			baseF0 = 10.0f; // whisper
		}
	}
	else if (config.mode == VoicingMode::Whisper && std::fabs(baseF0 - 220.0f) < 0.01f)
	{
		baseF0 = 10.0f;
	}

	m_voiceSource = activeMode;
	m_baseF0 = baseF0;

	for (size_t wi = 0; wi < words.size(); ++wi)
	{
		std::string w = words[wi];
		char punct = '\0';
		while (!w.empty() && (w.back() == '.' || w.back() == ',' || w.back() == '!' || w.back() == '?' || w.back() == ';' || w.back() == ':'))
		{
			if (punct == '\0') punct = w.back();
			w.pop_back();
		}

		if (w.empty())
			continue;

		float wordF0 = baseF0;

		if (config.repoJumpingPitch && activeMode != VoicingMode::Whisper)
		{
			// R.E.P.O. discrete pitch states: [-jitter, 0, +jitter]
			int r = static_cast<int>(rng() % 3);
			float offset = 0.0f;
			if (r == 0) offset = -config.jitterHz;
			else if (r == 2) offset = config.jitterHz;

			wordF0 = baseF0 + offset;
			if (wordF0 < 5.0f) wordF0 = 5.0f;
		}

		std::string phStr = KlatterTranslator::Get().TextToPhonemes(w);
		if (!phStr.empty())
		{
			SynthesizeWordUnit(phStr, wordF0, msPerFrame, audioFloat);
		}

		// Inter-word and punctuation cadence pauses (identical to R.E.P.O. / Unity SpeechSynth)
		if (wi + 1 < words.size())
		{
			float gapSec = 0.040f; // 40ms standard inter-word gap
			if (punct == ',' || punct == ';' || punct == ':')
				gapSec = 0.180f; // 180ms comma pause
			else if (punct == '.' || punct == '!' || punct == '?')
				gapSec = 0.350f; // 350ms sentence-ending pause

			size_t gapSamples = static_cast<size_t>(m_sr * gapSec);
			audioFloat.insert(audioFloat.end(), gapSamples, 0.0f);
		}
	}

	return ConvertFloatToPCMWithLimiter(audioFloat, 0.85f);
}

} // namespace Klatter
