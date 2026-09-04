# Bell Pickup

An electronic adapter/pickup that retrofits onto a **real** handbell (one
with an actual clapper) to sense rings and trigger sound electronically —
unlike [`../electric-handbell`](../electric-handbell/), which is a
fully 3D-printed bell body with no moving parts, this attaches to an
existing physical bell.

**Status: planned.** Nothing has been designed yet; this folder is
scaffolding, kept separate from
[`../electric-handbell`](../electric-handbell/) so this product's files
never mix with that one's.

## Anticipated layout

```
3dprint/    Mounting/adapter hardware for attaching the pickup to a real bell
firmware/   Sensor + wireless firmware for the pickup
```

Likely shares a fair amount of firmware DNA with
[`../electric-handbell/firmware`](../electric-handbell/firmware/)
(accelerometer-based ring detection) — worth starting from a copy of that
rather than from scratch, though the mounting/sensing details will differ
since this senses a real clapper strike rather than being cast into a
purpose-built body.

Add a `CHANGELOG.md` here once there's a first release to log. Adjust the
layout above as the actual design takes shape — it's a starting point, not
a commitment.
