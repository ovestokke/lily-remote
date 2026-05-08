# Loose Internal Part / Opening Notes

**Date:** 2026-05-04  
**Device:** LILYGO T5 E-Paper S3 Pro Lite  
**Symptom:** Something is loose/rattling inside the unit. Device still powers/works. It looks like the loose part may be visible/moving behind the rear M4 holes near the top, possibly the battery or another internal part.

## Current conclusion

We looked for an official LILYGO document explaining how to open/disassemble the **T5 E-Paper S3 Pro Lite** shell.

**No official opening/disassembly procedure was found.**

Sources checked:

- Official product page: <https://lilygo.cc/products/t5-e-paper-s3-pro-lite>
- Official Lite wiki: <https://wiki.lilygo.cc/get_started/en/Wearable/T5-E-Paper-Lite/T5-E-Paper-Lite.html>
- Official Pro wiki: <https://wiki.lilygo.cc/get_started/en/Wearable/T5-E-Paper-S3-Pro/T5-E-Paper-S3-Pro.html>
- Official GitHub repo: <https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO>
- Older related issue: <https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/issues/93>

## What official docs/photos do confirm

The official Lite wiki/product docs confirm or show:

- The Lite/Pro unit is a phone-like enclosed device.
- It has rear mounting/screw holes visible in official product photos.
- The wiki lists **6 × 3.8 mm mounting holes**.
- The product page “What's in the box” image shows the device plus **2 × screw**.
- The official GitHub repo includes mechanical files:
  - `DXF/H752-Shell size.dwg`
  - `DXF/H752-Board size.dxf`

However, none of these are an official safe opening sequence.

## Important clarification about issue #93

Issue #93 in `Xinyuan-LilyGO/LilyGo-EPD47` shows that someone opened/modded an older T5-4.7" S3 board/display assembly:

<https://github.com/Xinyuan-LilyGO/LilyGo-EPD47/issues/93>

This is **not** the same as the new phone-style **T5 E-Paper S3 Pro Lite** shell, and it is **not** an official disassembly guide.

## Support issue opened

A new issue has been opened against the official current device repo:

<https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO/issues/23>

Issue summary:

> I just received my T5 E-paper S3 Pro Lite today. But it has something loose inside, likely the battery. I can see it move in the M4 holes on the top. Is there a good way to open it, to secure what's loose, and check for damage?

## Recommended next action

Wait for LILYGO/seller response before opening, because this may be an arrival defect/RMA issue.

Suggested follow-up question to LILYGO:

```md
Can LILYGO please confirm the official safe opening procedure?

Specifically:
1. Which screws should be removed?
2. Are there plastic clips or adhesive holding the back cover?
3. Is the wireless charging coil or battery attached to the back cover?
4. Will opening it void warranty/RMA?
5. If the internal battery is loose on arrival, should I open and secure it, or should this be returned/replaced?

I do not want to damage the e-paper glass, battery, or wireless charging coil by guessing.
```

## Safety notes until confirmed

Because this is a LiPo-powered device with wireless charging:

- Do not charge unattended while something is loose inside.
- Avoid shaking or stressing the device.
- If the battery is swollen, hot, punctured, smells chemical/sweet, or the shell becomes warm unexpectedly, stop using it.
- Prefer official support/RMA before opening if warranty matters.

## If opening becomes necessary later

Do not treat this as official guidance. These are only constraints to keep in mind if LILYGO confirms opening is acceptable:

- Use a soft cloth under the glass/e-paper side.
- Avoid prying against the front glass/e-paper panel.
- Expect screws plus possible plastic clips/adhesive.
- Open slowly in case the wireless charging coil, battery, speaker/vibrator, or cables are attached to the back cover.
- Do not use metal tools near the LiPo pouch or PCB.
- If securing a loose LiPo, use electronics-safe double-sided foam tape/Kapton-style retention without compressing, puncturing, or gluing the pouch rigidly.
