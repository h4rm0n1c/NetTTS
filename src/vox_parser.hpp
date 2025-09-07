#pragma once
#include <string>

// Transform plain text into FlexTalk VOX style with vendor tags.
// - Wraps with \!wH1 ... \!wH0 (trailing space after \!wH0 for safety)
// - Inserts \!br at sentence ends & cadence points
// - Uses a generic prosody engine (no word-specific hacks)
std::wstring vox_process(const std::wstring& in, bool wrap_vox_tags);
