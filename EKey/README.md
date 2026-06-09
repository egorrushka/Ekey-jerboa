
<!-- ════════════════════════════════════════════════════════════════════════ -->
<!--  HEADER                                                                    -->
<!-- ════════════════════════════════════════════════════════════════════════ -->

<div align="center">

# 🦘 EKey-Jerboa

### GPU-accelerated search tool for the Bitcoin "puzzle" challenges

<p>
  <img src="https://img.shields.io/badge/version-1.0.0--beta-9b59b6?style=for-the-badge" alt="version">
  <img src="https://img.shields.io/badge/status-BETA%20%E2%80%94%20testing-e67e22?style=for-the-badge" alt="status">
  <img src="https://img.shields.io/badge/license-GPLv3-2ea44f?style=for-the-badge" alt="license">
</p>
<p>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-2b5797?style=flat-square" alt="platform">
  <img src="https://img.shields.io/badge/GPU-NVIDIA%20CUDA-76b900?style=flat-square&logo=nvidia&logoColor=white" alt="cuda">
  <img src="https://img.shields.io/badge/CUDA-13.1-76b900?style=flat-square" alt="cuda version">
  <img src="https://img.shields.io/badge/made%20in-Ukraine%20%F0%9F%87%BA%F0%9F%87%A6-005bbb?style=flat-square" alt="made in ukraine">
</p>

<!-- ── Screenshots (replace the two files in /assets) ──────────────────────── -->
<br>

<img src="/docs%20screenshots/ui.png" alt="EKey-Jerboa launcher" width="80%">

<br><br>

<img src="/docs%20screenshots/cmd.png" alt="EKey-Jerboa console" width="80%">

<br><br>

<!-- ── Buttons ─────────────────────────────────────────────────────────────── -->
<a href="../../releases/latest">
  <img src="https://img.shields.io/badge/%E2%AC%87%20Download-Latest%20Release-2ea44f?style=for-the-badge" alt="download">
</a>
&nbsp;
<a href="../../issues">
  <img src="https://img.shields.io/badge/%F0%9F%90%9B%20Report-Bug-c0392b?style=for-the-badge" alt="report bug">
</a>
&nbsp;
<a href="#-usage">
  <img src="https://img.shields.io/badge/%F0%9F%93%96%20Read-Manual-2b5797?style=for-the-badge" alt="manual">
</a>

</div>

---

> [!WARNING]
> **Beta release — under active testing.** This is an early public build:
> behaviour may change and bugs are still possible. Use at your own risk and
> please report anything odd via **Issues**. Feedback is very welcome. 🙏

---

## 📖 What it is

Given a target **P2PKH address** (or compressed public key) and a **key range**,
EKey-Jerboa sweeps that range on the GPU looking for the matching private key.

The range (a **chunk**) is split into many equal **slots**. The program scans
those slots in **random order**, hopping between them on a timer and saving
progress, so a search can be paused and resumed at any time.

---

## ⭐ Full coverage — random order, *no gaps*

> [!IMPORTANT]
> **Running EKey-Jerboa continuously covers the entire chunk — with no skipped
> keys.** The random slot order is a **full-period LCG permutation**: it is a
> true *bijection* over all slots, so **every slot is visited exactly once per
> pass — none skipped, none repeated.**
>
> In other words: it behaves **almost like a plain sequential brute-force** — the
> *coverage* is identical (100 % of the chunk) — the **only** difference is the
> *order* (pseudo-random instead of ascending). Left running, it will go through
> the whole chunk completely.

How the two run modes achieve this:

| Mode | Flag | Behaviour | Coverage |
|------|------|-----------|----------|
| **Full scan** | `-T 999999999` | Reads each slot **to the end** before moving to the next | Whole chunk, slot by slot |
| **Timed jumps** | `-T <sec>` + `-b` | Hops between slots on a timer; each slot's position is **saved**, so across hops every slot is still finished | Whole chunk, interleaved |

Either way the **LCG permutation guarantees completeness** — random *path*,
sequential-grade *coverage*.

---

## 🚀 Features

<table>
<tr>
<td width="50%" valign="top">

**🎯 Target matching**
P2PKH compressed address (`-a`) or compressed public key (`-p`).

**🔀 Random slot order**
Full-period LCG permutation — every slot visited exactly once per pass.

**🪓 Deep slot split (`-D`)**
Split the chunk by the *N*-th hex symbol into equal slots.

</td>
<td width="50%" valign="top">

**⏱️ Time jumps (`-T`)**
Hop to the next slot every *T* seconds, or full-scan each slot.

**💾 Resume & honest completion (`-b`)**
Per-slot progress is saved; the search ends only when every slot is fully read.

**📊 Live console**
Current slot, hop counter, completed slots, keys this hop, progress bar, speed, ETA to next hop.

**🖥️ Graphical launcher**
A Tkinter front-end that builds the command line for you.

</td>
</tr>
</table>

**Slot split levels:**

| Flag | Splits by | Multiplier |
|------|-----------|-----------|
| `OFF` / `-D3` | 3rd hex symbol | ×16 |
| `-D4` | 4th hex symbol | ×256 |
| `-D5` | 5th hex symbol | ×4096 |
| `-D6` | 6th hex symbol | ×65536 |

> Slot count = `#selected slots × 16^(N-2)`.

---

## 🧩 Requirements

- **NVIDIA GPU** + **CUDA Toolkit** (developed / tested on CUDA 13.1).
- **Windows:** Visual Studio build tools &nbsp;|&nbsp; **Linux:** `g++` + `make`.

---

## 🔧 Build

<details open>
<summary><b>Windows</b></summary>

A ready `compile_modern.bat` is included — just run it. It produces
`EKey-Jerboa.exe`.

</details>

<details>
<summary><b>Linux</b></summary>

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

</details>

> [!NOTE]
> **GPU architectures:** builds target `sm_75` (RTX 20xx), `sm_86`
> (RTX 30xx / A4000), `sm_89` (RTX 40xx) out of the box, plus `compute_89`
> PTX so newer cards (RTX 50xx+) run via JIT. Edit `GENCODE` / `-arch` to match
> your card.

---

## 📖 Usage

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
| `-D4..-D6`    | Split deeper (OFF / default = `-D3`) |
| `-faq` / `-inf` | Full manual / version & credits |

> Run `EKey-Jerboa -faq` for the full in-program manual.

### 🖥️ Launcher

A graphical launcher lets you pick the puzzle / address, set the chunk on a
visual ruler or in hex, choose grid / GPU / jump interval / split level, toggle
progress save-resume, and save / restore sessions. It only starts
`EKey-Jerboa` with the right flags — **all GPU work is done by the compiled
program itself.**

> [!NOTE]
> **Launcher safety:** the launcher is shipped compiled to an `.exe` only to
> protect authorship. It contains **no viruses and no hidden code**. If you
> prefer not to run the `.exe`, ignore it entirely and run `EKey-Jerboa`
> directly from the command line with the flags above — the launcher is only a
> convenience front-end.

---

## 👤 Credits

- **Fork author / maintainer:** [egorrushka](https://github.com/egorrushka)
- **Based on:** [VanitySearch](https://github.com/JeanLucPons/VanitySearch) by **Jean Luc PONS** (GPLv3)
- **Programmed by:** Claude (Anthropic, model *Claude Opus 4.8*) under the
  meticulous guidance and direction of egorrushka — who designed the engine
  behaviour, drove the architecture and tested every single build.
- **Made in:** 🇺🇦 Ukraine, under the extreme conditions of war, in the glorious
  city of **Chernihiv**.

---

## 📜 License

This project is licensed under the **GNU General Public License v3** — see
[`LICENSE`](LICENSE). It is a fork based on VanitySearch (also GPLv3); the
GPLv3 notice in every source file is preserved.

---

## ✉️ Contact

Questions / feedback: **egor.gr1@gmail.com**

<div align="center">
<br>
<sub>Made with care in Chernihiv, Ukraine 🇺🇦</sub>
</div>
