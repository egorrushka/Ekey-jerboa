// Jerboa.cpp
// EKey-Jerboa V1.0.0  -  Deep subslot engine (slot split by Nth hex symbol)
// Author fork: egorrushka
// Based on VanitySearch by Jean Luc PONS (GPLv3)
//
// All scatter code (Jerboa golden-ratio scatter + PR progressive deepening)
// has been removed. The Deep engine is now the single working engine.

#include "Vanity.h"
#include "Jerboa.h"
#include "Timer.h"
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <fcntl.h>     // O_WRONLY, open
#include <unistd.h>    // dup, dup2, close, STDOUT_FILENO
#endif
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <ctime>
#include <climits>
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>
using namespace std;

// ── helpers ───────────────────────────────────────────────────────────────────

static string i2h(const Int& v){ return const_cast<Int&>(v).GetBase16(); }
static string trim0(const string& h){
    size_t n=h.find_first_not_of('0');
    return (n!=string::npos)?h.substr(n):"0";
}

static bool h2i(const string& s, Int& out){
    string h=s;
    if(h.size()>=2&&h[0]=='0'&&(h[1]=='x'||h[1]=='X')) h=h.substr(2);
    if(h.empty()) return false;
    while(h.size()<64) h.insert(h.begin(),'0');
    vector<char> b(h.begin(),h.end()); b.push_back('\0');
    out.SetBase16(b.data()); return true;
}

// ── Progress file naming ─────────────────────────────────────────────────────
// Puzzle number derived from hex_lo (e.g. "400000000000000000" -> 71).
static int puzzleFromHex(const string& hex_lo) {
    if(hex_lo.empty()) return 0;
    int bits = (int)(hex_lo.size() - 1) * 4;
    char c = (char)tolower((unsigned char)hex_lo[0]);
    if     (c>='8') bits += 4;
    else if(c>='4') bits += 3;
    else if(c>='2') bits += 2;
    else            bits += 1;
    return bits;
}

// ── mkdir helper (cross-platform) ────────────────────────────────────────────
#ifdef _WIN32
#include <direct.h>
static void mkdirIfNeeded(const char* path){ _mkdir(path); }
#else
#include <sys/stat.h>
static void mkdirIfNeeded(const char* path){ mkdir(path, 0755); }
#endif

// ════════════════════════════════════════════════════════════════════════════
// DeepEngine implementation — slot split by the Nth hex symbol (-D3..-D5)
// Author: egorrushka  |  EKey-Jerboa V1.0.0  |  GPLv3
// ════════════════════════════════════════════════════════════════════════════

// ── LCG full-period multiplier (Hull-Dobell theorem) ─────────────────────────
// Returns 'a' such that x = (a*x + 1) % m has full period m.
static int64_t deep_findLCGA(int64_t m) {
    if (m <= 1) return 1;
    int64_t temp = m, prod = 1;
    for (int64_t p = 2; p * p <= temp; p++) {
        if (temp % p == 0) {
            if (prod % p != 0) prod *= p;
            while (temp % p == 0) temp /= p;
        }
    }
    if (temp > 1 && prod % temp != 0) prod *= temp;
    // If 4|m -> ensure 4|prod
    if (m % 4 == 0 && prod % 4 != 0) prod *= 2;
    return prod + 1;
}

// ── actual subslot index (Random LCG order only) ─────────────────────────────
static inline int64_t deep_actualIdx(const DeepEngine& e) {
    return e.lcgState;
}

// ── resume buffer (alloc + zero) ──────────────────────────────────────────────
// Allocates one int per subslot (totalSubslots). DEEP_RESUME_MAX (~4.2M, fits
// -D6 over a full single-puzzle range) caps it: if totalSubslots exceeds the
// cap, per-slot resume is disabled and the engine falls back to infinite
// Random sampling (a setup warning is printed).
static void deep_allocResume(DeepEngine& e) {
    int64_t want = e.totalSubslots;
    if (want > DEEP_RESUME_MAX) want = 0;   // safety: too many slots, disable resume
    if (e.slotPos && e.slotPosCap == want) {
        for (int64_t i = 0; i < want; i++) e.slotPos[i] = 0;
        return;
    }
    if (e.slotPos) { delete[] e.slotPos; e.slotPos = nullptr; }
    e.slotPosCap = want;
    if (want > 0) {
        e.slotPos = new int[(size_t)want];
        for (int64_t i = 0; i < want; i++) e.slotPos[i] = 0;
    }
}

// ── file helpers ─────────────────────────────────────────────────────────────
static void deep_computeSaveFileBase(DeepEngine& e, BITCRACK_PARAM* bc) {
    string lo = trim0(i2h(bc->ksStart));
    string hi = trim0(i2h(bc->ksFinish));
    int puz = puzzleFromHex(lo);
    string lo8 = lo.size()>8 ? lo.substr(0,8) : lo;
    string hi8 = hi.size()>8 ? hi.substr(0,8) : hi;
    const char* mode = e.randMode ? "rnd" : "seq";
    snprintf(e.progressDir, sizeof(e.progressDir),
             "d%d_%d_0x%s-0x%s_W%d_%s",
             e.deepSymbol, puz, lo8.c_str(), hi8.c_str(),
             e.gridProfile, mode);
    snprintf(e.saveFileBase, sizeof(e.saveFileBase),
             "%s/deep_%d_%s_%s",
             e.progressDir, puz, lo8.c_str(), hi8.c_str());
    e.puzNum = puz;
}

static string deep_jsonFile   (const DeepEngine& e, int gpu) {
    return string(e.saveFileBase) + "_gpu" + to_string(gpu) + ".json";
}
static string deep_launcherFile(const DeepEngine& e, int gpu) {
    return string(e.saveFileBase) + "_gpu" + to_string(gpu) + ".launcher";
}
static void deep_ensureDir(const DeepEngine& e) { mkdirIfNeeded(e.progressDir); }

// ── window update ─────────────────────────────────────────────────────────────
static void deep_updateWindow(DeepEngine& e, BITCRACK_PARAM* bc) {
    // actualIdx = which subslot index to search
    int64_t actualIdx = deep_actualIdx(e);

    // subslotStart = chunkStart + actualIdx * subslotSize
    Int offset; offset.Set(&e.subslotSize);
    offset.Mult((uint64_t)actualIdx);
    e.subslotStart.Set(&e.chunkStart);
    e.subslotStart.Add(&offset);

    e.subslotEnd.Set(&e.subslotStart);
    e.subslotEnd.Add(&e.subslotSize);
    Int one; one.SetInt32(1);
    e.subslotEnd.Sub(&one);
    if (e.subslotEnd.IsGreater(&e.chunkEnd)) e.subslotEnd.Set(&e.chunkEnd);

    bc->ksStart.Set(&e.subslotStart);
    bc->ksFinish.Set(&e.subslotEnd);

    // Display hex of the current slot start.
    string sh = trim0(i2h(e.subslotStart));
    // Full hex (for the "Current Slot" line).
    strncpy(e.currentSlotFullHex, sh.c_str(), sizeof(e.currentSlotFullHex)-1);
    e.currentSlotFullHex[sizeof(e.currentSlotFullHex)-1] = 0;
    // Short prefix: include the 1st symbol; show >=4 chars (more for deeper D).
    int nchars = e.deepSymbol < 4 ? 4 : e.deepSymbol;
    int want = min((int)sh.size(), nchars);
    want = max(want, 1);
    strncpy(e.currentSubslotHex, sh.substr(0, want).c_str(),
            sizeof(e.currentSubslotHex)-1);
    e.currentSubslotHex[sizeof(e.currentSubslotHex)-1] = 0;
}

// ── compute total batches per subslot ────────────────────────────────────────
static void deep_computeTotalBatches(DeepEngine& e) {
    Int num; num.Set(&e.subslotSize);
    Int denom; denom.SetInt32(e.numThreads);
    denom.Mult((uint64_t)e.STEP_SIZE_val);
    Int dm1; dm1.Set(&denom); dm1.SubOne();
    num.Add(&dm1);
    num.Div(&denom);
    uint64_t tb = num.bits64[0];
    if (num.bits64[1] || tb > (uint64_t)INT_MAX) tb = INT_MAX;
    e.totalBatches = (int)(tb < 1 ? 1 : tb);
}

// ── setup ─────────────────────────────────────────────────────────────────────
void deep_setup(DeepEngine& e, int numThreads, int STEP_SIZE,
                BITCRACK_PARAM* bc, int gpuId) {
    e.numThreads    = numThreads;
    e.STEP_SIZE_val = STEP_SIZE;
    e.J_sec         = bc->jerboaJumpSec;
    e.deepSymbol    = bc->deepMode;
    if (e.deepSymbol < 3) e.deepSymbol = 3;
    if (e.deepSymbol > 6) e.deepSymbol = 6;
    e.randMode      = true;          // Random only (Sequential removed)
    e.gridProfile   = bc->gridProfile;

    e.chunkStart.Set(&bc->ksStart);
    e.chunkEnd.Set(&bc->ksFinish);
    e.chunkRange.Set(&e.chunkEnd);
    e.chunkRange.Sub(&e.chunkStart);
    Int one; one.SetInt32(1);
    e.chunkRange.Add(&one);

    // Determine number of hex digits in the chunk's top address
    string endHex = trim0(i2h(e.chunkEnd));
    int H = (int)endHex.size();
    if (H < e.deepSymbol) {
        printf("[DeepMode] ERROR: deepSymbol %d >= key hex digits %d\n",
               e.deepSymbol, H);
        return;
    }

    // subslotSize = 16^(H - deepSymbol)
    e.subslotSize.SetInt32(1);
    for (int i = 0; i < H - e.deepSymbol; i++)
        e.subslotSize.Mult((uint64_t)16);

    // totalSubslots = ceil(chunkRange / subslotSize)
    Int ts; ts.Set(&e.chunkRange);
    Int ssz; ssz.Set(&e.subslotSize);
    Int dm1; dm1.Set(&ssz); dm1.SubOne();
    ts.Add(&dm1);
    ts.Div(&ssz);
    e.totalSubslots = (ts.bits64[1] || ts.bits64[0] > (uint64_t)INT64_MAX)
                      ? INT64_MAX : (int64_t)ts.bits64[0];
    if (e.totalSubslots < 1) e.totalSubslots = 1;

    // Random LCG full-period order (Sequential removed)
    e.lcgA     = deep_findLCGA(e.totalSubslots);
    e.lcgState = 0; // first subslot = index 0

    e.subslotSeqIdx = 0;
    e.batchCount    = 0;
    e.slotsCompleted = 0;
    e.passCount      = 0;

    deep_allocResume(e);
    deep_computeSaveFileBase(e, bc);
    deep_updateWindow(e, bc);
    deep_computeTotalBatches(e);

    e.jumpStartTime = Timer::get_tick();
    e.speedMkey     = 1000.0;
    e.lastSnap      = 0; e.lastSpeedTime = 0.0;
    e.displayShown  = false; e.inited = true;

    deep_ensureDir(e);
    printf("[DeepMode] ProgDir     : %s\n", e.progressDir);
    printf("[DeepMode] Symbol      : D%d  (%s)\n",
           e.deepSymbol, e.randMode ? "Random" : "Sequential");
    printf("[DeepMode] TotalSlots  : %lld\n", (long long)e.totalSubslots);
    printf("[DeepMode] BatchPerSlot: %d  Jump: %.1f sec%s\n",
           e.totalBatches, e.J_sec,
           (e.J_sec >= 999999998.0) ? "  (No-Jump / Sequential)" : "");
    if (e.slotPosCap == 0)
        printf("[DeepMode] WARNING     : too many slots, per-slot resume disabled\n");
}

// ── advance to next subslot (with resume + honest completion) ─────────────────
// Returns false when the whole chunk has been fully read.
bool deep_advance(DeepEngine& e, BITCRACK_PARAM* bc) {
    // 1. Save the position of the slot we are leaving.
    int64_t curIdx = deep_actualIdx(e);
    int curBatch = e.batchCount;
    if (curBatch > e.totalBatches) curBatch = e.totalBatches;
    if (e.slotPos && curIdx < e.slotPosCap) {
        bool wasDone = (e.slotPos[curIdx] >= e.totalBatches);
        e.slotPos[curIdx] = curBatch;
        if (!wasDone && curBatch >= e.totalBatches) e.slotsCompleted++;
    }

    // Honest completion: every slot fully read.
    if (e.slotPos && e.slotsCompleted >= e.totalSubslots)
        return false;

    // 2. Advance to the next NOT-yet-finished slot.
    bool found = false;
    for (int64_t tries = 0; tries < e.totalSubslots; tries++) {
        e.subslotSeqIdx++;
        if (e.subslotSeqIdx >= e.totalSubslots) {
            e.subslotSeqIdx = 0;
            e.passCount++;
        }
        // Random LCG full-period step (Sequential removed)
        e.lcgState = (e.lcgA * e.lcgState + 1) % e.totalSubslots;

        int64_t nextIdx = deep_actualIdx(e);
        if (!e.slotPos || nextIdx >= e.slotPosCap) { found = true; break; } // no resume -> accept
        if (e.slotPos[nextIdx] < e.totalBatches)   { found = true; break; } // not done -> use it
    }
    if (!found) return false;   // nothing left to do

    // 3. Restore the position of the slot we land on.
    int64_t newIdx = deep_actualIdx(e);
    e.batchCount = (e.slotPos && newIdx < e.slotPosCap) ? e.slotPos[newIdx] : 0;

    deep_updateWindow(e, bc);
    deep_computeTotalBatches(e);
    return true;
}

// ── save ─────────────────────────────────────────────────────────────────────
void deep_save(const DeepEngine& e, int gpuId) {
    deep_ensureDir(e);
    FILE* f = fopen(deep_jsonFile(e, gpuId).c_str(), "w");
    if (!f) return;
    fprintf(f, "{\n");
    fprintf(f, "  \"deep_symbol\":     %d,\n",    e.deepSymbol);
    fprintf(f, "  \"total_subslots\":  %lld,\n",  (long long)e.totalSubslots);
    fprintf(f, "  \"subslot_seq\":     %lld,\n",  (long long)e.subslotSeqIdx);
    fprintf(f, "  \"lcg_state\":       %lld,\n",  (long long)e.lcgState);
    fprintf(f, "  \"lcg_a\":           %lld,\n",  (long long)e.lcgA);
    fprintf(f, "  \"batch_count\":     %d,\n",    e.batchCount);
    fprintf(f, "  \"total_batches\":   %d,\n",    e.totalBatches);
    fprintf(f, "  \"slots_completed\": %lld,\n",  (long long)e.slotsCompleted);
    fprintf(f, "  \"pass_count\":      %lld,\n",  (long long)e.passCount);
    fprintf(f, "  \"rand_mode\":       %s,\n",    e.randMode ? "true" : "false");
    fprintf(f, "  \"grid_profile\":    %d,\n",    e.gridProfile);
    fprintf(f, "  \"chunk_start\":     \"%s\",\n",trim0(i2h(e.chunkStart)).c_str());
    fprintf(f, "  \"chunk_end\":       \"%s\",\n",trim0(i2h(e.chunkEnd)).c_str());

    // Per-slot resume positions. Can be large (up to ~1M ints) — build a single
    // buffer and write it in one go to keep this fast.
    fprintf(f, "  \"slot_pos\": [");
    if (e.slotPos && e.slotPosCap > 0) {
        string buf;
        buf.reserve((size_t)e.slotPosCap * 4 + 16);
        char num[16];
        for (int64_t i = 0; i < e.slotPosCap; i++) {
            int v = e.slotPos[i];
            int len = snprintf(num, sizeof(num), "%d", v);
            buf.append(num, len);
            if (i < e.slotPosCap - 1) buf.push_back(',');
        }
        fwrite(buf.data(), 1, buf.size(), f);
    }
    fprintf(f, "]\n}\n");
    fclose(f);
}

// ── load ─────────────────────────────────────────────────────────────────────
bool deep_load(DeepEngine& e, int gpuId, BITCRACK_PARAM* bc) {
    // Folder/file name depends on deepSymbol/randMode/gridProfile — take them
    // from the current launch so resume looks in the matching folder.
    e.deepSymbol  = bc->deepMode;
    if (e.deepSymbol < 3) e.deepSymbol = 3;
    if (e.deepSymbol > 6) e.deepSymbol = 6;
    e.randMode    = true;          // Random only
    e.gridProfile = bc->gridProfile;
    deep_computeSaveFileBase(e, bc);
    FILE* f = fopen(deep_jsonFile(e, gpuId).c_str(), "r");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long sz = ftell(f); rewind(f);
    if (sz <= 0) { fclose(f); return false; }
    string s((size_t)sz, '\0');
    fread(&s[0], 1, (size_t)sz, f);
    fclose(f);

    auto getI64 = [&](const char* key) -> int64_t {
        string k = string("\"") + key + "\":";
        auto p = s.find(k); if (p==string::npos) return 0;
        p = s.find_first_of("-0123456789", p+k.size());
        return (p==string::npos) ? 0LL : (int64_t)strtoll(s.c_str()+p,nullptr,10);
    };
    auto getI = [&](const char* key) -> int { return (int)getI64(key); };
    auto getB = [&](const char* key) -> bool {
        string k = string("\"") + key + "\": true";
        return s.find(k) != string::npos;
    };

    e.deepSymbol     = getI("deep_symbol");
    e.totalSubslots  = getI64("total_subslots");
    e.subslotSeqIdx  = getI64("subslot_seq");
    e.lcgState       = getI64("lcg_state");
    e.lcgA           = getI64("lcg_a");
    e.batchCount     = getI("batch_count");
    e.totalBatches   = getI("total_batches");
    e.slotsCompleted = getI64("slots_completed");
    e.passCount      = getI64("pass_count");
    e.randMode       = getB("rand_mode");
    e.gridProfile    = getI("grid_profile");

    if (e.deepSymbol < 3 || e.deepSymbol > 6) e.deepSymbol = 3;
    if (e.totalSubslots <= 0) return false;

    // Allocate resume buffer and parse slot_pos[] if present.
    deep_allocResume(e);
    if (e.slotPos && e.slotPosCap > 0) {
        string key = "\"slot_pos\": [";
        size_t p = s.find(key);
        if (p != string::npos) {
            size_t cur = p + key.size();
            int64_t idx = 0;
            while (idx < e.slotPosCap && cur < s.size() && s[cur] != ']') {
                e.slotPos[idx++] = atoi(s.c_str() + cur);
                while (cur < s.size() && s[cur] != ',' && s[cur] != ']') cur++;
                if (cur < s.size() && s[cur] == ',') cur++;
            }
        }
        // Recount completed slots from the restored array (authoritative).
        int64_t done = 0;
        for (int64_t i = 0; i < e.slotPosCap; i++)
            if (e.slotPos[i] >= e.totalBatches) done++;
        e.slotsCompleted = done;
    }
    return true;
}

void deep_save_launcher(const DeepEngine& e, int gpuId, const char* lj) {
    if (!lj || !lj[0]) return;
    deep_ensureDir(e);
    FILE* f = fopen(deep_launcherFile(e, gpuId).c_str(), "w");
    if (!f) return;
    fprintf(f, "%s\n", lj); fclose(f);
}

// ── display ───────────────────────────────────────────────────────────────────
void deep_display(DeepEngine& e, uint64_t totalKeys) {
    // Speed
    double now = Timer::get_tick();
    if (e.lastSpeedTime > 0.0 && now - e.lastSpeedTime > 0.5) {
        uint64_t delta = totalKeys - e.lastSnap;
        e.speedMkey = (double)delta / ((now - e.lastSpeedTime) * 1e6);
    }
    e.lastSnap = totalKeys; e.lastSpeedTime = now;

    // ETA
    double rem = e.J_sec - (now - e.jumpStartTime);
    if (rem < 0) rem = 0;
    int rm = (int)(rem/60), rs = (int)rem % 60;
    char eta[16]; snprintf(eta, sizeof(eta), "%dm%02ds", rm, rs);

    // Speed string
    char spd[16];
    if (e.speedMkey >= 1.0) snprintf(spd, sizeof(spd), "%.2f Gk/s", e.speedMkey);
    else snprintf(spd, sizeof(spd), "%.0f Mk/s", e.speedMkey*1000.0);

    // Progress %
    double pct = e.totalBatches > 0
        ? (double)e.batchCount * 100.0 / (double)e.totalBatches : 0.0;
    if (pct > 100.0) pct = 100.0;

    // Progress bar (32 chars)
    int filled = (int)(32.0 * pct / 100.0);
    char bar[36] = {};
    for (int i = 0; i < 32; i++) bar[i] = (i < filled) ? '#' : '.';
    bar[32] = 0;

    // Enable ANSI colors once (Windows VT). colorOK gates red highlighting.
    static int colorOK = -1;
    if (colorOK < 0) {
#ifdef _WIN32
        HANDLE hc = GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD m = 0; colorOK = 0;
        if (GetConsoleMode(hc, &m) &&
            SetConsoleMode(hc, m | 0x0004 /*ENABLE_VIRTUAL_TERMINAL_PROCESSING*/))
            colorOK = 1;
#else
        colorOK = 1;
#endif
    }
    // Wrap the first redN chars of a hex string in red (the D-defining prefix).
    auto colorize = [&](const string& hex, int redN) -> string {
        if (!colorOK || redN <= 0) return hex;
        int n = min(redN, (int)hex.size());
        return string("\x1b[91m") + hex.substr(0, n) + "\x1b[0m" + hex.substr(n);
    };
    int redN = e.deepSymbol;       // slot-identity prefix = first N hex symbols

    // Live position: a clean visual sweep that threads across the WHOLE slot
    // (steps of subslotSize/256), advancing each refresh. The real per-hop
    // coverage of such a huge slot is ~0%, so this is a visual scanner over the
    // slot range (each value is a real address inside the slot). CPU-only math.
    string liveHex;
    {
        static uint64_t sweepCtr = 0;
        sweepCtr++;
        Int incr; incr.Set(&e.subslotSize);
        Int d256((uint64_t)256); incr.Div(&d256);          // subslotSize / 256
        Int off; off.Set(&incr); off.Mult((uint64_t)(sweepCtr % 256));
        Int liveKey; liveKey.Set(&e.subslotStart);
        liveKey.Add(&off);
        if (liveKey.IsGreater(&e.subslotEnd)) liveKey.Set(&e.subslotEnd);
        liveHex = trim0(i2h(liveKey));
    }

    // Keys done in the current hop (comma-grouped).
    char keystr[32];
    {
        char raw[24]; snprintf(raw, sizeof(raw), "%llu",
                               (unsigned long long)e.slotKeyCount);
        int len = (int)strlen(raw), j = 0;
        for (int i = 0; i < len; i++) {
            if (i > 0 && (len - i) % 3 == 0) keystr[j++] = ',';
            keystr[j++] = raw[i];
        }
        keystr[j] = 0;
    }

    // L[0]: WHERE we are — slot prefix (D-symbols in red), hop #, completion.
    {
        char tail[160];
        snprintf(tail, sizeof(tail),
                 "  hop %lld/%lld  |  done %lld/%lld slots  pass %lld",
                 (long long)(e.subslotSeqIdx + 1),
                 (long long)e.totalSubslots,
                 (long long)e.slotsCompleted,
                 (long long)e.totalSubslots,
                 (long long)(e.passCount + 1));
        e._L[0] = "[D" + to_string(e.deepSymbol) + " Random] Slot: 0x" +
                  colorize(string(e.currentSubslotHex), redN) + tail;
    }
    // L[1]: live HEX position on the current slot (D-prefix in red).
    {
        e._L[1] = "[Current Slot : 0x" + colorize(liveHex, redN) + "]";
    }
    // L[2]: keys scanned in the current hop (live).
    {
        char tmp[48];
        snprintf(tmp, sizeof(tmp), "[Key: %s]", keystr);
        e._L[2] = tmp;
    }
    // L[3]: progress through the current slot + speed + time to next hop.
    {
        char tmp[160];
        snprintf(tmp, sizeof(tmp),
                 "  slot progress [%s] %6.2f%%   %s   next hop %s",
                 bar, pct, spd, eta);
        e._L[3] = tmp;
    }

    const int NLINES = 4;

    // Render
#ifdef _WIN32
    static COORD dRow = {0,0};
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hOut, &csbi);
    if (!e.displayShown) {
        dRow = {0, csbi.dwCursorPosition.Y};
        e.displayShown = true;
    }
    COORD cur = dRow;
    for (int i = 0; i < NLINES; i++) {
        SetConsoleCursorPosition(hOut, cur);
        DWORD wr;
        FillConsoleOutputCharacterA(hOut, ' ', csbi.dwSize.X, cur, &wr);
        SetConsoleCursorPosition(hOut, cur);
        printf("%s\n", e._L[i].c_str());
        cur.Y++;
    }
    SetConsoleCursorPosition(hOut, {0, (SHORT)(dRow.Y + NLINES)});
#else
    if (!e.displayShown) e.displayShown = true;
    for (int i = 0; i < NLINES; i++) printf("%s\n", e._L[i].c_str());
#endif
    fflush(stdout);
}

// ── findKeyGPU_Deep ───────────────────────────────────────────────────────────
void VanitySearch::findKeyGPU_Deep(TH_PARAM* ph) {

    static bool      deepSetupDone = false;
    static DeepEngine deepEng;

    BITCRACK_PARAM* bc = ph->obj->bc;

    // ── GPU init ──────────────────────────────────────────────────────────────
    GPUEngine g(ph->gpuId, maxFound);
    if (!g.IsInitialised()) { ph->isRunning = false; return; }
    if (!deepSetupDone) printf("[+] GPU: %s\n", g.deviceName.c_str());

    if (searchType == PUBKEY) {
        g.SetTargetPublicKey(targetPubKeyX, targetPubKeyParity);
    } else {
        g.SetSearchType(searchType);
        g.SetAddress(usedAddressL, nbAddress);
        if (nbAddress == 1 && onlyFull && !usedAddress.empty()) {
            int pp = usedAddress[0];
            if (addresses[pp].items && !addresses[pp].items->empty()) {
                uint8_t* h160 = (*addresses[pp].items)[0].hash160;
                g.SetHash160Target(h160, true);
            }
        }
    }

    int numThreadsGPU = g.GetNbThread();
    int STEP_SIZE     = g.GetStepSize();
    int GROUP_SIZE    = g.GetGroupSize();

    // ── One-time setup ────────────────────────────────────────────────────────
    if (!deepSetupDone) {
        deepSetupDone = true;
        if (backupMode && deep_load(deepEng, ph->gpuId, bc)) {
            // Restore state
            deepEng.chunkStart.Set(&bc->ksStart);
            deepEng.chunkEnd.Set(&bc->ksFinish);
            deepEng.chunkRange.Set(&deepEng.chunkEnd);
            deepEng.chunkRange.Sub(&deepEng.chunkStart);
            Int one_r; one_r.SetInt32(1); deepEng.chunkRange.Add(&one_r);
            deepEng.deepSymbol    = bc->deepMode;
            if (deepEng.deepSymbol < 3) deepEng.deepSymbol = 3;
            if (deepEng.deepSymbol > 6) deepEng.deepSymbol = 6;
            deepEng.randMode      = true;
            deepEng.gridProfile   = bc->gridProfile;
            deepEng.numThreads    = numThreadsGPU;
            deepEng.STEP_SIZE_val = STEP_SIZE;
            deepEng.J_sec         = bc->jerboaJumpSec;
            // Recompute subslotSize from H and deepSymbol
            string endHex = trim0(i2h(deepEng.chunkEnd));
            int H = (int)endHex.size();
            deepEng.subslotSize.SetInt32(1);
            for (int i = 0; i < H - deepEng.deepSymbol; i++)
                deepEng.subslotSize.Mult((uint64_t)16);
            deep_updateWindow(deepEng, bc);
            deep_computeTotalBatches(deepEng);
            deepEng.jumpStartTime = Timer::get_tick();
            deepEng.speedMkey     = 1000.0;
            deepEng.inited        = true;
            printf("[DeepMode] Resumed: D%d  subslot %lld/%lld  batch %d/%d  done %lld/%lld\n",
                   deepEng.deepSymbol,
                   (long long)deepEng.subslotSeqIdx,
                   (long long)deepEng.totalSubslots,
                   deepEng.batchCount, deepEng.totalBatches,
                   (long long)deepEng.slotsCompleted,
                   (long long)deepEng.totalSubslots);
        } else {
            printf("[DeepMode] Starting fresh.\n");
            deep_setup(deepEng, numThreadsGPU, STEP_SIZE, bc, ph->gpuId);
        }
        if (bc->launcherJson[0])
            deep_save_launcher(deepEng, ph->gpuId, bc->launcherJson);
        // Write an initial checkpoint immediately so the progress folder and
        // .json exist from second 0 for every mode (incl. OFF/-D3), instead of
        // only after the first time-jump.
        if (backupMode) deep_save(deepEng, ph->gpuId);
        fflush(stdout);
    }

    // ── Main loop ──────────────────────────────────────────────────────────────
    bool ok = true;
    vector<ITEM> found;
    double tLastSave = Timer::get_tick();
    while (!endOfSearch) {
        ok = true;

        if (Pause.load()) { Timer::SleepMillis(100); continue; }

        // ── Set GPU starting keys (resume offset = batchCount) ────────────────
        {
            uint64_t progress = (uint64_t)deepEng.batchCount * (uint64_t)STEP_SIZE;
            Point* pk2 = new Point[numThreadsGPU];
            getGPUStartingKeys(bc->ksStart, bc->ksFinish,
                               GROUP_SIZE, numThreadsGPU, pk2, progress);
            ok = g.SetKeys(pk2);
            delete[] pk2;
        }

        int idxcount = deepEng.batchCount;
        deepEng.slotKeyCount = 0;
        double tLastDisp = Timer::get_tick();
        deep_display(deepEng, counters[ph->threadId]);

        // ── Inner batch loop ──────────────────────────────────────────────────
        while (!endOfSearch && !Pause.load()) {

            if (!g.Launch(found, false)) { ok=false; break; }
            idxcount++;
            deepEng.batchCount++;
            counters[ph->threadId] += (uint64_t)numThreadsGPU * (uint64_t)STEP_SIZE;
            deepEng.slotKeyCount   += (uint64_t)numThreadsGPU * (uint64_t)STEP_SIZE;

            // ── Found keys ────────────────────────────────────────────────────
            if (!found.empty()) {
                Int stepThread;
                stepThread.Set(&bc->ksFinish);
                stepThread.Sub(&bc->ksStart);
                stepThread.AddOne();
                Int ntI; ntI.SetInt32(numThreadsGPU);
                stepThread.Div(&ntI);
                Int keycount((uint64_t)((uint64_t)(idxcount-1)*(uint64_t)STEP_SIZE));

                for (int fi = 0; fi < (int)found.size() && !endOfSearch; fi++) {
                    ITEM it = found[fi];
                    Int part; part.Set(&stepThread);
                    part.Mult((uint64_t)it.thId);
                    Int privkey; privkey.Set(&bc->ksStart);
                    privkey.Add(&part);
                    privkey.Add(&keycount);

                    if (searchType == PUBKEY) {
                        Int k(&privkey);
                        if (it.incr < 0) { k.Add((uint64_t)(-it.incr)); k.Neg(); k.Add(&secp->order); }
                        else              k.Add((uint64_t)it.incr);
                        output(inputAddresses[0], secp->GetPrivAddress(true,k), k.GetBase16());
                        nbFoundKey++; updateFound();
                    } else {
                        checkAddr(*(address_t*)(it.hash), it.hash,
                                  privkey, it.incr, it.endo, it.mode);
                    }
                }
                found.clear();
            }

            // ── Periodic display update (every 2s) ───────────────────────────
            {
                double nowD = Timer::get_tick();
                if (nowD - tLastDisp >= 1.0) {
                    deep_display(deepEng, counters[ph->threadId]);
                    tLastDisp = nowD;
                }
            }

            // ── Auto-save ─────────────────────────────────────────────────────
            if (backupMode) {
                double now2 = Timer::get_tick();
                if (now2 - tLastSave >= 60.0) {
                    deep_save(deepEng, ph->gpuId);
                    tLastSave = now2;
                }
            }

            // ── Subslot exhausted (this slot fully read) ──────────────────────
            if (deepEng.batchCount >= deepEng.totalBatches &&
                deepEng.totalBatches < (INT_MAX - 1)) {
                if (backupMode) deep_save(deepEng, ph->gpuId);
                if (!deep_advance(deepEng, bc)) {
                    printf("\n[DeepMode] Chunk fully exhausted. Search complete.\n");
                    fflush(stdout);
                    if (backupMode) deep_save(deepEng, ph->gpuId);
                    endOfSearch = true;
                }
                idxcount = 0; break;
            }

            // ── Time-based jump ───────────────────────────────────────────────
            if (Timer::get_tick() - deepEng.jumpStartTime >= deepEng.J_sec) {
                if (backupMode) deep_save(deepEng, ph->gpuId);
                if (!deep_advance(deepEng, bc)) {
                    printf("\n[DeepMode] Chunk fully exhausted. Search complete.\n");
                    fflush(stdout);
                    if (backupMode) deep_save(deepEng, ph->gpuId);
                    endOfSearch = true;
                    idxcount = 0; break;
                }
                deepEng.jumpStartTime = Timer::get_tick();
                deep_display(deepEng, counters[ph->threadId]);
                idxcount = 0; break;
            }
        } // inner
    } // outer

    if (backupMode) deep_save(deepEng, ph->gpuId);
    ph->isRunning = false;
}
