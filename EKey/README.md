# EKey-Jerboa

**GPU-accelerated search tool for the Bitcoin "puzzle" challenges.**

Given a target P2PKH address (or compressed public key) and a key range,
EKey-Jerboa sweeps that range on the GPU looking for the matching private key.
The range (a *chunk*) is split into many equal *slots* that the program scans
in random order, hopping between them on a timer and saving progress so a search
can be paused and resumed.

---

## Credits

- **Fork author / maintainer:** [egorrushka](https://github.com/egorrushka)
- **Based on:** [VanitySearch](https://github.com/JeanLucPons/VanitySearch) by **Jean Luc PONS** (GPLv3)
- **Programmed by:** Claude (Anthropic, model *Claude Opus 4.8*) under the
  meticulous guidance and direction of egorrushka — who designed the engine
  behaviour, drove the architecture and tested every single build.
- **Made in:** Ukraine, under the extreme conditions of war, in the glorious
  city of **Chernihiv**.

---

## Features

- Target match by P2PKH compressed address (`-a`) or compressed public key (`-p`).
- **Deep slot split (`-D`)** — the chunk is split by the *N*-th hex symbol into
  equal slots: `OFF/-D3 → ×16`, `-D4 → ×256`, `-D5 → ×4096`, `-D6 → ×65536`
  (slot count = `#selected slots × 16^(N-2)`).
- **Random slot order** — full-period LCG permutation; every slot is visited
  exactly once per pass, in pseudo-random order.
- **Time jumps (`-T`)** — hop to the next slot every *T* seconds; `-T 999999999`
  reads each slot to the end before moving on.
- **Resume & honest completion (`-b`)** — per-slot progress is saved; a resumed
  run continues where it stopped and the search ends only when every slot is
  fully read.
- **Live console** — current slot, hop counter, completed slots, keys this hop,
  slot progress bar, speed and time to the next hop.
- **Graphical launcher** (Python/Tkinter) that builds the command line for you.

---

## Requirements

- NVIDIA GPU + **CUDA Toolkit** (developed/tested on CUDA 13.1).
- Windows: Visual Studio build tools. &nbsp;|&nbsp; Linux: `g++` + `make`.

---

## Build

### Windows
A ready `compile_modern.bat` is included — just run it. It produces
`EKey-Jerboa.exe`.

### Linux
A `Makefile` is included. From the project root:

```bash
make            # or: make -j$(nproc)
```

Override architecture / paths if needed:

```bash
make GENCODE="-gencode arch=compute_86,code=sm_86"
make CUDA_PATH=/usr/local/cuda-13.1
```

The sources are cross-platform — nothing needs editing.

> **GPU architectures:** builds target `sm_75` (RTX 20xx), `sm_86`
> (RTX 30xx / A4000), `sm_89` (RTX 40xx) out of the box, plus `compute_89`
> PTX so newer cards (RTX 50xx+) run via JIT. Edit `GENCODE` / `-arch` to match
> your card.

---

## Usage

```
EKey-Jerboa -a <address> -s 0x<start> -e 0x<end> [options]
```

| Option | Meaning |
|--------|---------|
| `-a <addr>`   | Target P2PKH (compressed) address |
| `-p <pubkey>` | Target compressed public key |
| `-s 0x<hex>`  | Chunk start |
| `-e 0x<hex>`  | Chunk end |
| `-r <bits>`   | Bit-range shorthand (e.g. `-r 71`) |
| `-T <sec>`    | Jump interval (default 30). `999999999` = full scan |
| `-G <id>`     | GPU device id (default 0) |
| `-W <0-7>`    | Grid profile (`0`=auto, `5`=6144x256, `7`=12288x256) |
| `-b`          | Save / resume progress |
| `-D4..-D6`    | Split deeper (OFF/default = `-D3`) |
| `-faq` / `-inf` | Full manual / version & credits |

Run `EKey-Jerboa -faq` for the full in-program manual.

### Launcher
A graphical launcher lets you pick the puzzle/address, set the chunk on a visual
ruler or in hex, choose grid / GPU / jump interval / split level, toggle progress
save-resume, and save/restore sessions. It only starts `EKey-Jerboa` with the
right flags — all GPU work is done by the compiled program itself.

> **Launcher safety:** the launcher is shipped compiled to an `.exe` only to
> protect authorship. It contains no viruses and no hidden code. If you prefer
> not to run the `.exe`, ignore it entirely and run `EKey-Jerboa` directly from
> the command line with the flags above — the launcher is only a convenience
> front-end.

---

## License

This project is licensed under the **GNU General Public License v3** — see
[`LICENSE`](LICENSE). It is a fork based on VanitySearch (also GPLv3); the GPLv3
notice in every source file is preserved.

## Contact

Questions / feedback: **egor.gr1@gmail.com**
