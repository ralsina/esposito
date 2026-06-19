# Pet

A virtual pet that lives on your Esposito device. Feed it, play with it, keep it clean and rested — it ages in real time, evolves through life stages, and persists between launches.

Neglect it and it gets sick; neglect it longer and... well. Press **New** to start over with a fresh egg.

## Stats

- **Hunger** — drops over time; restore with **Feed**
- **Happy** — drops over time; restore with **Play**
- **Energy** — drops while awake; toggle **Sleep** to restore it
- **Clean** — drops over time (and when it makes a mess); restore with **Clean**
- **Health** — falls when other needs hit zero or when sick; recovers when comfortable. At zero, the pet dies.

Stats decay based on **real elapsed time**, so the pet changes while the app is closed — check in on it regularly.

## Life stages

The pet evolves with age: **Egg** (1 min) → **Baby** (10 min) → **Child** (1 hour) → **Teen** (1 day) → **Adult**.

## Controls

| Key | Action |
|-----|--------|
| `F` | Feed |
| `P` | Play |
| `S` | Sleep / wake |
| `C` | Clean |
| `M` | Medicine (cures sickness) |
| `R` | New egg (when the pet has died) |
| Ctrl+Esc | Back to launcher |

On-screen buttons are also touch-tappable.

## Build

```sh
bash scripts/build_app.sh -l ui2 apps/pet/app.c
```

Then copy to SD card:

```text
/sdcard/apps/pet/program.elf
```
