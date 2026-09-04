# EHB — Electric Handbells

Umbrella repo for a family of electric/electronic handbell products. Each
product's source is self-contained under `products/<name>/` — its own
README, 3D-print files, firmware, app code, and changelog live there and
don't mix with any other product's.

## Products

| Folder | Product | Status |
|---|---|---|
| [`products/electric-handbell`](products/electric-handbell/) | Solid-state electronic handbell: 3D-printed bell body with no moving parts, accelerometer-triggered wireless sound (BLE) | Active — v0.2 released |
| [`products/song-bell`](products/song-bell/) | Simpler 3D-printed bell that plays a song on its own | Planned |
| [`products/bell-pickup`](products/bell-pickup/) | Electronic adapter/pickup that retrofits onto a real (physical-clapper) handbell to trigger sound electronically | Planned |

See each product's own README for hardware, firmware, and build details.

## Repo conventions

- One folder per product under `products/`, each independently buildable —
  no product's firmware/app/model files reference another's.
- A shared root [`.gitignore`](.gitignore) covers common build/editor
  junk (Gradle, Arduino/PlatformIO, Python, OpenSCAD backups) across all
  products, since those patterns aren't product-specific.
- Per-product changelogs (e.g.
  [`products/electric-handbell/CHANGELOG.md`](products/electric-handbell/CHANGELOG.md))
  rather than one repo-wide log, since products version independently.
