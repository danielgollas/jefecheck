# Transcript — Weekly Standup

**[00:00] Luis Ortega:** Okay, let's get started. Quick round of updates and then the release.

**[00:06] Dana Kim:** The FreeType regression is still biting us. Text metrics come back wrong on Linux after the atlas change.

**[00:20] Luis Ortega:** Is that the invalidation bug you mentioned yesterday?

**[00:31] Dana Kim:** Yes. The atlas cache isn't invalidated when the font size changes mid-frame, so glyphs render from the stale texture.

**[00:53] Dana Kim:** I have a fix sketched, but it needs a day of testing on all three platforms.

**[01:11] Luis Ortega:** Then let's not rush it. I'd rather slip the release than ship broken text rendering.

**[01:36] Luis Ortega:** Decision: we move one point seven from Wednesday to Friday.

**[01:51] Luis Ortega:** Dana, you own the atlas fix.

**[01:58] Dana Kim:** I'll have a build for QA by Thursday morning.

**[31:15] Luis Ortega:** Alright, that's everything. Thanks all.
