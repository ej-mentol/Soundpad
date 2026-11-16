#pragma once

#include <cstdint>
#include <cstddef>

namespace Klatter
{

struct PhonemeParam
{
    float a;
    float b;
    int16_t c;
    int16_t d;
    int16_t e;
};

struct PhonemeInfo
{
    int16_t dur;
    int16_t t1;
    int16_t t2;
    int32_t flags;
    PhonemeParam params[19];
};

struct PhonemeMapping
{
    uint32_t code;
    uint8_t count;
    uint8_t indices[5];
};

struct TranslationRule
{
    const char* left;
    const char* body;
    const char* right;
    const char* phonemes;
};

extern const float g_GainTable[88];
extern const PhonemeMapping g_PhonemeMappings[52];
extern const size_t g_NumPhonemeMappings;
extern const PhonemeInfo g_PhonemeData[69];
extern const size_t g_NumPhonemeData;

extern const TranslationRule* const g_RulesIndex[27];
extern const size_t g_RulesCount[27];

} // namespace Klatter
