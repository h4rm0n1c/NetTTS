## NetTTS Prosody Encoder (VOX Parser)

### What it is
NetTTS isn’t just a bridge to an old TTS engine.

It includes a **rule-based prosody layer** that takes ordinary text and reshapes it into something the SAPI-4 / FlexTalk engine can deliver with better timing and clarity.

In practice:

- **Input:** chat text  
- **Output:** the same text, with added pauses and control cues  
- **Goal:** speech that’s **clear, firm, and announcement-like**, not just readable

The code lives mainly in `vox_parser.cpp` / `vox_parser.hpp`.

---

### Why it exists
The constraints were straightforward:

- People type messages naturally (chat, commands, short announcements)
- It has to work live (streams, overlays, triggers)
- The TTS engine is deterministic and has no modern prosody model

So the solution is also straightforward:

- explicit rules  
- token classification  
- small amounts of scoring  
- deterministic transforms  

That keeps it **fast**, **low-memory**, **predictable**, and **easy to reason about**.

---

### Where the cadence comes from
The heuristics were shaped using two main sources:

- Facility and PA-style announcements (notably the Black Mesa announcement system)
- Spoken delivery from *Half-Life* (1998)

Valve’s recordings in *Half-Life 1* provide a large amount of clear, controlled, intelligible speech designed to cut through noise and distraction.  
This project takes that style and implements it as a **rules-based front end**, rather than learned or statistical prosody.

In short: Valve supplied the voice and delivery model; NetTTS implements the logic around it.

---

### What it does (roughly)
The VOX parser:

1. **Tokenizes the text**  
   Words, numbers, punctuation, symbols. Tracks casing and position.

2. **Assigns a rough “weight” to tokens**  
   - Light: articles and linkers  
   - Medium: ordinary content words  
   - Heavy: proper nouns, numbers, units, longer or stressed words  

3. **Inserts breaks to create cadence**  
   It uses `\!br` markers to segment speech into small beats, based on:
   - punctuation  
   - arrival of heavy tokens  
   - isolating linkers (`in`, `to`, `of`, `for`, etc.)  
   - clause-like boundaries  
   - limits on how long speech can run without a pause

4. **Normalizes toward an announcement style**  
   - reshapes numbers, times, and units  
   - handles single letters so they don’t blur together  
   - cleans spacing and break placement

5. **Outputs a control-flavoured text stream**  
   Still text — but now acting more like instructions than prose.

---

### “Better input gives better output”
NetTTS doesn’t try to flatten everything into the same delivery.

Instead, it creates a feedback loop:

- casual or messy input → intelligible but plain speech  
- structured, intentional input → noticeably better rhythm and clarity  

People usually get better results if they:
- use punctuation  
- capitalize names and places  
- write numbers and units explicitly  
- use location-style phrasing when appropriate

---

### Why it works well live
- no models, no warm-up  
- very low overhead  
- predictable timing  
- viewers naturally adapt their input without special syntax

---

### Where else it fits
Because it prioritizes clarity and segmentation, it works well for:
- announcements
- radio-style output
- status readouts
- accessibility or noisy environments

---

### In one sentence
NetTTS doesn’t just send text to a voice —  
it **parses and reshapes it**, using rules inspired by *Half-Life*-era announcement delivery, so the engine speaks it better.
