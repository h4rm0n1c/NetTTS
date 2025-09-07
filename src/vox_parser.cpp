#include "vox_parser.hpp"
#include <vector>
#include <regex>
#include <cwctype>
#include <sstream>
#include <algorithm>

static inline std::wstring trim(const std::wstring& s){
    size_t a=0,b=s.size();
    while(a<b && iswspace(s[a])) ++a;
    while(b>a && iswspace(s[b-1])) --b;
    return s.substr(a,b-a);
}
static inline std::wstring to_lower(std::wstring s){
    for(auto& c: s) c=(wchar_t)towlower(c);
    return s;
}
static inline bool is_all_digits(const std::wstring& t){
    if(t.empty()) return false;
    for(wchar_t c: t) if(!iswdigit(c)) return false;
    return true;
}
static inline bool is_titlecase_word(const std::wstring& t){
    if(t.empty()) return false;
    if(!iswalpha(t[0]) || !iswupper(t[0])) return false;
    return true;
}
static inline std::wstring strip_trailing_punct(std::wstring t){
    while(!t.empty()){
        wchar_t c = t.back();
        if(c==L','||c==L'.'||c==L';'||c==L':'||c==L'!'||c==L'?'||c==L'"'||c==L'\'') t.pop_back();
        else break;
    }
    return t;
}
static inline bool ends_with_br(const std::wstring& s){
    size_t n = s.size();
    return n >= 5 && s.compare(n-5, 5, L" \\!br") == 0;
}

// Sentence splitter (keeps terminator punctuation attached)
static std::vector<std::wstring> split_sentences(const std::wstring& in){
    std::vector<std::wstring> out;
    std::wstring cur;
    for(size_t i=0;i<in.size();++i){
        wchar_t c = in[i];
        cur.push_back(c);
        if(c==L'.' || c==L'!' || c==L'?'){
            size_t j=i+1;
            while(j<in.size() && (in[j]==L'"' || in[j]==L'\'')) { cur.push_back(in[j]); ++j; }
            out.push_back(trim(cur));
            cur.clear();
            i = j-1;
        }
    }
    std::wstring tail = trim(cur);
    if(!tail.empty()) out.push_back(tail);
    return out;
}

// Lead-in break after short opening phrases
static void apply_leadin_break(std::wstring& s){
    static const std::wregex r(LR"(^\s*((?:Now|Please|A\s+reminder|On\s+behalf\s+of|Good\s+(?:morning|evening)|This\s+(?:automated\s+train|tram)))\b)",
                               std::regex_constants::icase);
    std::wsmatch m;
    if(std::regex_search(s, m, r)){
        std::wstring head = trim(m.str(0));
        std::wstring rest = trim(s.substr(m.length()));
        s = head + L" \\!br " + rest;
    }
}


// “the” -> “thee”
//  - Backward rule: after preps (to/for/of/in/on/at) or "on behalf of"
//  - NEW Forward rule: before TitleCase, single letter (X or X:), or a number
// Robust across \!br tags; preserves trailing punctuation.
static void apply_thee_rule(std::wstring& s){
    // tokenize by whitespace; keep tags as tokens
    std::wstringstream ss(s);
    std::vector<std::wstring> tok; std::wstring t;
    while (ss >> t) tok.push_back(t);
    if (tok.empty()) return;

    auto is_tag = [](const std::wstring& w){
        return (w==L"\\!br" || w==L"\\!wH1" || w==L"\\!wH0");
    };
    auto to_lower_copy = [](std::wstring v){ for (auto& c: v) c=(wchar_t)towlower(c); return v; };

    // split trailing punctuation; return (core, punctTail)
    auto split_tail = [](const std::wstring& x)->std::pair<std::wstring,std::wstring>{
        std::wstring core=x, tail;
        while (!core.empty()){
            wchar_t c = core.back();
            if (c==L','||c==L'.'||c==L';'||c==L':'||c==L'!'||c==L'?'||c==L'"'||c==L'\''){
                tail.insert(tail.begin(), c);
                core.pop_back();
            } else break;
        }
        return {core, tail};
    };

    auto clean_lower = [&](const std::wstring& x)->std::wstring{
        auto p = split_tail(x);
        return to_lower_copy(p.first);
    };
    auto bare = [&](const std::wstring& x)->std::wstring{
        return split_tail(x).first;
    };

    auto prev_word = [&](int i, int nth)->std::wstring{
        for (int k=i-1; k>=0; --k){
            if (is_tag(tok[k])) continue;
            if (--nth==0) return clean_lower(tok[k]);
        }
        return L"";
    };
    auto next_word = [&](int i, int nth)->std::wstring{
        for (int k=i+1; k<(int)tok.size(); ++k){
            if (is_tag(tok[k])) continue;
            if (--nth==0) return tok[k]; // not lowered; we want case info
        }
        return L"";
    };

    auto is_prep = [&](const std::wstring& w)->bool{
        std::wstring tl = to_lower_copy(w);
        return (tl==L"to"||tl==L"for"||tl==L"of"||tl==L"in"||tl==L"on"||tl==L"at");
    };
    auto is_title = [&](const std::wstring& w)->bool{
        auto b = bare(w);
        return !b.empty() && iswalpha(b[0]) && iswupper(b[0]);
    };
    auto is_single_letter = [&](const std::wstring& w)->bool{
        auto b = bare(w);
        if (b.size()==1 && iswalpha(b[0]) && iswupper(b[0])) return true;
        if (b.size()==2 && iswalpha(b[0]) && iswupper(b[0]) && b[1]==L':') return true;
        return false;
    };
    auto is_number = [&](const std::wstring& w)->bool{
        auto b = bare(w);
        if (b.empty()) return false;
        for (wchar_t c: b) if (!iswdigit(c)) return false;
        return true;
    };

    for (int i=0; i<(int)tok.size(); ++i){
        if (is_tag(tok[i])) continue;

        auto [core, tail] = split_tail(tok[i]);
        std::wstring cl = to_lower_copy(core);
        if (cl != L"the") { tok[i] = core + tail; continue; }

        bool make_thee = false;

        // Backward (prepositions, incl. "on behalf of")
        std::wstring p1 = prev_word(i, 1);
        if (!p1.empty()){
            if (is_prep(p1)) make_thee = true;
            else if (to_lower_copy(p1) == L"of"){
                std::wstring p2 = prev_word(i, 2);
                std::wstring p3 = prev_word(i, 3);
                if (to_lower_copy(p2)==L"behalf" && to_lower_copy(p3)==L"on") make_thee = true;
            }
        }

        // Forward (before proper names / letters / numbers)
        if (!make_thee){
            std::wstring n1 = next_word(i, 1);
            if (!n1.empty()){
                if (is_title(n1) || is_single_letter(n1) || is_number(n1)){
                    make_thee = true;
                }
            }
        }

        tok[i] = (make_thee ? L"thee" : core) + tail;
    }

    // rebuild
    std::wstring out;
    for (size_t i=0;i<tok.size();++i){
        if (!out.empty()) out.push_back(L' ');
        out += tok[i];
    }
    s.swap(out);
}


// Times, degrees, and 3-digit decomposition
static void apply_time_numbers_degrees(std::wstring& s){
    // 12h time like 8:47 AM / A.M. / PM … (eat any trailing dot after meridiem)
    {
        std::wregex t12(LR"(\b(\d{1,2})[:.](\d{2})\s*((?:A\.?M\.?)|(?:P\.?M\.?))(?:\.)?\b)", std::regex_constants::icase);
        std::wstring out; out.reserve(s.size()+32);
        std::wsregex_iterator it(s.begin(), s.end(), t12), end;
        size_t last = 0;
        for(; it!=end; ++it){
            auto m = *it;
            out.append(s, last, m.position()-last);
            std::wstring hh = m.str(1);
            std::wstring mm = m.str(2);
            std::wstring ap = to_lower(m.str(3));
            std::wstring ap1 = (ap[0]==L'p') ? L"P:" : L"Ay:";
            out += hh + L" \\!br " + mm + L" \\!br " + ap1 + L" \\!br M:";
            last = m.position() + m.length();
        }
        out.append(s, last, std::wstring::npos);
        s.swap(out);
    }
    // “The time is 8 …” -> break before the hour
    s = std::regex_replace(s,
        std::wregex(LR"((\b[Tt]he\s+time\s+is)\s+(\d+))"),
        L"$1 \\!br $2");

    // 24h “HH00 hours” -> “HH \!br hundred \!br hours”
    s = std::regex_replace(s,
        std::wregex(LR"(\b(\d{2})00\s+hours\b)", std::regex_constants::icase),
        L"$1 \\!br hundred \\!br hours");

    // Three-digit non-round numbers: 105 -> “100 \!br and \!br 5” (strip leading zero in remainder)
    {
        std::wregex n3(LR"(\b([1-9])(\d)(\d)\b)");
        std::wstring out; out.reserve(s.size()+32);
        std::wsregex_iterator it(s.begin(), s.end(), n3), end;
        size_t last = 0;
        for(; it!=end; ++it){
            auto m = *it;
            std::wstring yz = m.str(2)+m.str(3);
            if (yz == L"00") continue; // round, leave as-is
            size_t nz = 0; while (nz < yz.size() && yz[nz]==L'0') ++nz;
            std::wstring rem = yz.substr(nz);
            out.append(s, last, m.position()-last);
            out += m.str(1)+L"00 \\!br and \\!br " + (rem.empty()? yz : rem);
            last = m.position() + m.length();
        }
        out.append(s, last, std::wstring::npos);
        s.swap(out);
    }

    // Degrees: “93 degrees” -> “93 \!br degrees \!br”
    s = std::regex_replace(s,
        std::wregex(LR"(\b(\d+)\s+degrees\b)", std::regex_constants::icase),
        L"$1 \\!br degrees \\!br");

    // Fix any lingering "M:." -> "M:" (meridiem shouldn’t carry a period)
    s = std::regex_replace(s, std::wregex(LR"(M:\s*\.)"), L"M:");
}


// Syllable heuristic
static int syllables(const std::wstring& w){
    if(w.empty()) return 0;
    std::wstring s; s.reserve(w.size());
    for(wchar_t c: w) s.push_back((wchar_t)towlower(c));
    auto isv=[&](wchar_t c){ return c==L'a'||c==L'e'||c==L'i'||c==L'o'||c==L'u'||c==L'y'; };
    int count=0; bool in=false;
    for(wchar_t c: s){ if(isv(c)){ if(!in){ ++count; in=true; } } else in=false; }
    if(count>1 && s.size()>1 && s.back()==L'e' && !(s.size()>2 && s[s.size()-2]==L'l')) --count; // silent 'e' (not "...le")
    if(s.size()>2 && s.back()==L'e' && s[s.size()-2]==L'l') ++count; // “…le”
    return std::max(1,count);
}

enum Wt { LIGHT, MEDIUM, HEAVY };

static bool is_stop(const std::wstring& tl){
    static const wchar_t* sw[] = {
        L"the",L"thee",L"a",L"an",L"to",L"for",L"of",L"in",L"on",L"at",L"by",L"with",L"from",
        L"as",L"is",L"are",L"was",L"were",L"this",L"that"
    };
    for(auto* w: sw) if(tl==w) return true;
    return false;
}
static bool is_lightverb(const std::wstring& tl){
    static const wchar_t* vw[] = {
        L"welcome",L"wait",L"stand",L"provide",L"provided",L"return",L"remain",L"maintain",
        L"maintained",L"verify",L"contact",L"board",L"arriving",L"inbound",L"bound",
        L"commence",L"commences"
    };
    for(auto* w: vw) if(tl==w) return true;
    return false;
}
static bool is_unit(const std::wstring& tl){
    static const wchar_t* uw[] = { L"degrees", L"hours", L"percent", L"%" };
    for(auto* w: uw) if(tl==w) return true;
    return false;
}

static Wt weight(const std::wstring& tok){
    if(tok.empty()) return LIGHT;
    std::wstring pfree = strip_trailing_punct(tok);
    std::wstring tl = to_lower(pfree);
    if(is_stop(tl) || is_lightverb(tl)) return LIGHT;
    if(is_all_digits(pfree)) return HEAVY;
    if(pfree.find(L'-')!=std::wstring::npos) return HEAVY;
    if(is_titlecase_word(pfree)) return HEAVY;
    if(is_unit(tl)) return HEAVY;
    int syl = syllables(pfree);
    if(syl>=3 || pfree.size()>=8) return HEAVY; // long or many-syllable content words
    return MEDIUM;
}

static bool is_and(const std::wstring& t){ return to_lower(strip_trailing_punct(t))==L"and"; }
static bool starts_with_punct(const std::wstring& t){
    if(t.empty()) return false;
    wchar_t c = t.front();
    return (c==L'.'||c==L','||c==L';'||c==L':'||c==L'!'||c==L'?'||c==L')');
}
static bool is_open_punct(const std::wstring& t){
    if(t.empty()) return false;
    wchar_t c=t.front(); return (c==L'('||c==L'"'||c==L'\'');
}

// Tokenize on whitespace (keep punctuation with token)
static std::vector<std::wstring> tokenize_ws(const std::wstring& s){
    std::wstringstream ss(s);
    std::vector<std::wstring> v; std::wstring t;
    while(ss >> t) v.push_back(t);
    return v;
}

// Insert TitleCase run breaks and Area/Level/Sector blocks
static void emit_titlecase_run(const std::vector<std::wstring>& v, size_t i, size_t j, std::wstring& out){
    // place a break before the run
    if(!out.empty() && !ends_with_br(out)) out += L" \\!br";
    for(size_t k=i;k<j;++k){
        out += L" " + v[k] + L" \\!br";
    }
}



// Core beat builder (single pass), generic prosody.
// Keeps: TitleCase runs, Area/Level/Sector, "and" join, soft head span,
//        comma/semicolon boundary, symmetric linker isolation [BR] linker [BR],
//        heavy-gate, 3-beat limiter.
// NEW:   Split hyphenated/en-dash/em-dash compounds into parts with breaks.
//        Standalone -, –, — act as hard breaks (not spoken).
static std::wstring build_beats(const std::wstring& sentence){
    auto v = tokenize_ws(sentence);
    if(v.empty()) return sentence;

    std::wstring out;
    bool since_break_all_light = true;
    Wt   prev_w = LIGHT;
    int  content_run = 0; // consecutive MEDIUM/HEAVY since last break

    auto reset_after_break = [&](){
        since_break_all_light = true;
        content_run = 0;
        prev_w = LIGHT;
    };

    auto bare = [](const std::wstring& w){ return strip_trailing_punct(w); };
    auto is_area_head = [&](const std::wstring& w){ return w==L"Area"||w==L"Level"||w==L"Sector"; };
    auto is_tag = [&](const std::wstring& w){ return (w==L"\\!br" || w==L"\\!wH1" || w==L"\\!wH0"); };
    auto lower = [&](std::wstring s){ return to_lower(std::move(s)); };

    auto is_single_letter = [&](const std::wstring& w)->bool{
        std::wstring b = bare(w);
        if (b.size()==1 && iswalpha(b[0]) && iswupper(b[0])) return true;          // C
        if (b.size()==2 && iswalpha(b[0]) && iswupper(b[0]) && b[1]==L':') return true; // C:
        return false;
    };

    // split trailing punctuation; return (core, punctTail)
    auto split_tail = [](const std::wstring& x)->std::pair<std::wstring,std::wstring>{
        std::wstring core=x, tail;
        while (!core.empty()){
            wchar_t c = core.back();
            if (c==L','||c==L'.'||c==L';'||c==L':'||c==L'!'||c==L'?'||c==L'"'||c==L'\''){
                tail.insert(tail.begin(), c);
                core.pop_back();
            } else break;
        }
        return {core, tail};
    };

    // emit hyphen/dash-split parts with breaks between them; keep tail on last part
    auto emit_dash_split = [&](const std::wstring& tok)->bool{
        auto [core, tail] = split_tail(tok);
        // find internal separators
        std::vector<size_t> cuts;
        for(size_t p=0;p<core.size();++p){
            wchar_t c = core[p];
            if (c==L'-' || c==0x2013 /*–*/ || c==0x2014 /*—*/){
                // only split if letters on both sides (avoid leading/trailing)
                if (p>0 && p+1<core.size() && iswalpha(core[p-1]) && iswalpha(core[p+1]))
                    cuts.push_back(p);
                else
                    return false; // treat as punctuation case elsewhere
            }
        }
        if (cuts.empty()) return false;

        size_t start = 0;
        for(size_t idx=0; idx<=cuts.size(); ++idx){
            size_t end = (idx<cuts.size() ? cuts[idx] : core.size());
            std::wstring part = core.substr(start, end-start);
            // spacing before part
            if(!out.empty() && !ends_with_br(out) && !starts_with_punct(part) && !is_open_punct(part))
                out.push_back(L' ');
            // last part gets the tail
            if (idx == cuts.size()) part += tail;
            out += part;
            // break after each part
            if(!ends_with_br(out)) out += L" \\!br";
            reset_after_break();
            start = end + (idx<cuts.size() ? 1 : 0);
        }
        return true;
    };

    for(size_t i=0;i<v.size();){
        // ---- Proper-noun blocks ----
        if (is_area_head(v[i])){
            size_t j=i+1;
            while(j<v.size()){
                std::wstring wj = bare(v[j]);
                if(is_all_digits(wj) || is_titlecase_word(wj)) ++j;
                else break;
            }
            if(j>i+1){
                emit_titlecase_run(v,i,j,out);
                i=j;
                reset_after_break();
                continue;
            }
        }
        if (is_titlecase_word(bare(v[i]))){
            size_t j=i;
            while(j<v.size() && is_titlecase_word(bare(v[j]))) ++j;
            if(j>=i+2){
                emit_titlecase_run(v,i,j,out);
                i=j;
                reset_after_break();
                continue;
            }
        }

        // ---- "and" between heavier items ----
        if(is_and(v[i]) && i>0 && i+1<v.size()){
            Wt lw = weight(v[i-1]);
            Wt rw = weight(v[i+1]);
            if(lw>=MEDIUM && rw>=MEDIUM){
                if(!ends_with_br(out)) out += L" \\!br";
                out += L" and \\!br";
                ++i;
                reset_after_break();
                continue;
            }
        }

        // ---- Standalone dash ( -, –, — ) => hard boundary ----
        if (v[i]==L"-" || v[i]==L"\u2013" || v[i]==L"\u2014"){
            if(!ends_with_br(out)) out += L" \\!br";
            reset_after_break();
            ++i;
            continue;
        }

        // ---- Symmetric linker isolation: [BR] linker [BR] ----
        {
            std::wstring tok = v[i];
            std::wstring tl  = lower(bare(tok));
            bool in_head_span = (i < 2);
            if (!in_head_span && (tl==L"at" || tl==L"in" || tl==L"on" || tl==L"to"
                               || tl==L"of" || tl==L"for"|| tl==L"from"|| tl==L"by")){
                if(!ends_with_br(out)) out += L" \\!br";        // BEFORE linker
                reset_after_break();

                if(!out.empty() && !starts_with_punct(tok) && !is_open_punct(tok)) out.push_back(L' ');
                out += tok;

                if(!ends_with_br(out)) out += L" \\!br";        // AFTER linker
                reset_after_break();

                ++i;
                continue;
            }
        }

// ---- Pre-copula break: put a beat BEFORE "is" when followed by determiner/contenty ----
{
    std::wstring tl_is  = to_lower(bare(v[i]));
    bool in_head_span   = (i < 2);
    auto is_tag_local   = [&](const std::wstring& w){ return (w==L"\\!br" || w==L"\\!wH1" || w==L"\\!wH0"); };

    auto is_single_letter = [&](const std::wstring& w)->bool{
        std::wstring b = strip_trailing_punct(w);
        if (b.size()==1 && iswalpha(b[0]) && iswupper(b[0])) return true;            // C
        if (b.size()==2 && iswalpha(b[0]) && iswupper(b[0]) && b[1]==L':') return true; // C:
        return false;
    };

    auto is_determiner = [&](const std::wstring& w)->bool{
        std::wstring b = to_lower(strip_trailing_punct(w));
        return (b==L"the"||b==L"thee"||b==L"a"||b==L"an"||
                b==L"this"||b==L"that"||b==L"these"||b==L"those"||
                b==L"some"||b==L"any"||b==L"each"||b==L"every"||b==L"no");
    };

    if (!in_head_span && tl_is == L"is") {
        // peek next non-tag token
        size_t k = i + 1;
        while (k < v.size() && is_tag_local(v[k])) ++k;

        bool trigger = false;
        if (k < v.size()) {
            const std::wstring& n1 = v[k];
            if (is_determiner(n1) ||
                is_titlecase_word(strip_trailing_punct(n1)) ||
                is_all_digits(strip_trailing_punct(n1)) ||
                is_single_letter(n1) ||
                weight(n1) == HEAVY)
            {
                trigger = true;
            }
        }

        if (trigger && !ends_with_br(out)) {
            out += L" \\!br";
            // reset run state like a real boundary
            since_break_all_light = true;
            content_run = 0;
            prev_w = LIGHT;
        }
    }
}

        // ---- Hyphenated / en-dash / em-dash compounds inside token ----
        if (emit_dash_split(v[i])) { ++i; continue; }

        // ---- Generic token logic ----
        std::wstring tok = v[i];
        Wt w_eff = weight(tok); // effective weight; may be softened at sentence start

        // Soft-start: first two tokens—avoid pre-breaks unless necessary.
        bool in_head_span = (i < 2);
        if (i==0 && is_titlecase_word(bare(tok))) {
            bool next_is_title = (i+1 < v.size()) && is_titlecase_word(bare(v[i+1]));
            if (!next_is_title && w_eff == HEAVY) w_eff = MEDIUM; // e.g., "Good evening"
        }

        // ---- HEAVY-GATE ----
        if (w_eff==HEAVY && content_run >= 1){
            int k = static_cast<int>(i) - 1;
            while (k >= 0 && is_tag(v[k])) --k;
            bool exempt = false;
            if (k >= 0){
                std::wstring prev = v[k];
                if (is_titlecase_word(bare(prev)) || is_all_digits(bare(prev)) || is_single_letter(prev)) {
                    exempt = true;
                }
            }
            if (!exempt){
                if(!ends_with_br(out)) out += L" \\!br";
                reset_after_break();
            }
        }

        // 3-beat limiter
        if (w_eff==HEAVY && content_run >= 2){
            if(!ends_with_br(out)) out += L" \\!br";
            reset_after_break();
        }

        // Heavy after only LIGHT (but not inside head span)
        if (w_eff==HEAVY && since_break_all_light && !in_head_span){
            if(!out.empty() && !ends_with_br(out)) out += L" \\!br";
            reset_after_break();
        }

        // Heavy-Heavy adjacency (outside TitleCase run)
        if (i>0 && prev_w==HEAVY && w_eff==HEAVY && !in_head_span){
            if(!ends_with_br(out)) out += L" \\!br";
            reset_after_break();
        }

        // Append token with spacing
        if(!out.empty() && !starts_with_punct(tok) && !is_open_punct(tok)) out.push_back(L' ');
        out += tok;

        // Punctuation boundary: break after ',' or ';'
        if (!tok.empty()){
            wchar_t last = tok.back();
            if ((last==L',' || last==L';') && !ends_with_br(out)){
                out += L" \\!br";
                reset_after_break();
                ++i;
                continue;
            }
        }

        // Update run stats
        if (w_eff==LIGHT){
            // remain in light span
        }else{
            ++content_run;               // MEDIUM or HEAVY
            since_break_all_light = false;
        }
        prev_w = w_eff;

        ++i;
    }

    return trim(out);
}




// Tidy spaces and tags
static std::wstring tidy(const std::wstring& s){
    std::wstring t = std::regex_replace(s, std::wregex(LR"(\s+)"), L" ");            // collapse spaces
    t = std::regex_replace(t, std::wregex(LR"(\s*\\!br\s*)"), L" \\!br ");           // normalize tag spacing
    t = std::regex_replace(t, std::wregex(LR"((\s*\\!br\s*){2,})"), L" \\!br ");     // collapse multiple breaks
    t = std::regex_replace(t, std::wregex(LR"(\s+([.,!?;:]))"), L"$1");              // no space before punctuation
    // trimyou 
    size_t a=0,b=t.size();
    while(a<b && iswspace(t[a])) ++a;
    while(b>a && iswspace(t[b-1])) --b;
    return t.substr(a,b-a);
}


// Convert standalone letters into stable "letter tokens" for FlexTalk.
// - "A:" / "a:" / bare "A"/"a" near a boundary -> "Ay:"
// - any single UPPERCASE letter near a boundary -> "X:" (exactly one colon)
// Boundaries are start/end or a \!br tag on either side. We preserve trailing punctuation.
static void normalize_letter_tokens(std::wstring& s){
    // split by whitespace; keep tags as tokens
    std::wstringstream ss(s);
    std::vector<std::wstring> tok; std::wstring t;
    while (ss >> t) tok.push_back(t);
    if (tok.empty()) return;

    auto is_tag = [](const std::wstring& w){
        return (w==L"\\!br" || w==L"\\!wH1" || w==L"\\!wH0");
    };

    // strip trailing punctuation, but lift out a single trailing ':' as a flag
    auto split_tail = [](const std::wstring& x)->std::tuple<std::wstring,bool,std::wstring>{
        std::wstring core = x, punct;
        bool had_colon = false;
        // peel general punctuation, but remember a single trailing ':'
        while(!core.empty()){
            wchar_t c = core.back();
            if(c==L':' && !had_colon){ had_colon = true; core.pop_back(); continue; }
            if(c==L','||c==L'.'||c==L';'||c==L'!'||c==L'?'||c==L'"'||c==L'\''){
                punct.insert(punct.begin(), c);
                core.pop_back();
                continue;
            }
            break;
        }
        return {core, had_colon, punct};
    };

    auto is_boundary = [&](int idx)->bool{
        return (idx < 0 || idx >= (int)tok.size() || is_tag(tok[idx]));
    };

    for (int i=0;i<(int)tok.size();++i){
        if (is_tag(tok[i])) continue;

        auto [coreOrig, had_colon, tail] = split_tail(tok[i]);
        std::wstring core = coreOrig;

        const bool near_left  = is_boundary(i-1);
        const bool near_right = is_boundary(i+1);
        const bool near_bound = (near_left || near_right);

        // Normalize A / a cases to "Ay:" when isolated or already written as A:
        if ((core==L"A" || core==L"a") && (near_bound || had_colon)){
            core = L"Ay";
            had_colon = true; // ensure exactly one colon added below
        }
        // Any other single UPPERCASE letter becomes a letter token near a boundary
        else if (core.size()==1 && iswalpha(core[0]) && iswupper(core[0]) && near_bound){
            // ensure exactly one colon
            // (if there was already one as trailing punct, we won't add another)
            // special "A" handled above
            had_colon = true;
        }

        // Reassemble: add colon iff had_colon is true, then the rest of punctuation
        std::wstring rebuilt = core;
        if (had_colon) rebuilt.push_back(L':');
        rebuilt += tail;

        tok[i] = rebuilt;
    }

    // Rebuild s (tidy() will normalize spaces/tags later)
    std::wstring out;
    for (size_t i=0;i<tok.size();++i){
        if (!out.empty()) out.push_back(L' ');
        out += tok[i];
    }
    s.swap(out);
}


std::wstring vox_process(const std::wstring& in, bool wrap_vox_tags){
    auto sents = split_sentences(in);
    std::wstring out;
    for (auto sent : sents){
        if (sent.empty()) continue;

        // ORDER: make "thee" first, then lead-in, then time/nums
        apply_thee_rule(sent);
        apply_leadin_break(sent);
        apply_time_numbers_degrees(sent);

        // beats + letter normalization
        std::wstring with_beats = build_beats(sent);
        normalize_letter_tokens(with_beats);
        with_beats = tidy(with_beats);

        // sentence-end cadence: add ~300ms pause, then a boundary
        if (!with_beats.empty() && with_beats.back()!=L' ') with_beats.push_back(L' ');
        with_beats += L"\\!sf500 \\!br";

        if (!out.empty()) out.push_back(L' ');
        out += with_beats;
    }

    out = trim(out);
    // collapse accidental double breaks from joins, normalize commas before breaks, etc.
    out = std::regex_replace(out, std::wregex(LR"((\s*\\!br\s*){2,})"), L" \\!br ");
    out = std::regex_replace(out, std::wregex(LR"(\\!br\s*([,;:]))"), L"$1 \\!br");

    if (!out.empty()) {
        if (wrap_vox_tags) {
            out = L"\\!wH1 " + out + L" \\!wH0 ";
        } else {
            out = out + L" ";
        }
    }
    return out;
}


