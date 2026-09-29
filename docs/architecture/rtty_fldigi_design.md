# RTTY in Lyra — cancelled

**Locked 2026-09-29 (N8SDR):** there is no native RTTY decoder or AFSK
modulator in Lyra.

Operators add fldigi, MMTTY, 2Tone, or any other digital program under
**Settings → Apps**, and use TCI / VAC as they already do for FT8.

Do not re-port fldigi RTTY into `src/dsp/`. CW receive (`cw_fldigi`) stays.
`FftFilt::rtty_filter` in that tree is the CW raised-cosine LPF, not a
RTTY modem.
