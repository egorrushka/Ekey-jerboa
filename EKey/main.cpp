// main.cpp
// EKey-Jerboa V1.0.0
// Author: egorrushka
// Based on VanitySearch by Jean Luc PONS (GPLv3)
// License: GPLv3 - https://www.gnu.org/licenses/
//
// Usage:
//   EKey-Jerboa.exe -a <address> -s 0xSTART -e 0xEND -T <seconds> [-W <0-10>] [-G <gpuId>] [-b]

#include <sstream>
#include "Timer.h"
#include "Vanity.h"
#include "Jerboa.h"
#include "SECP256k1.h"
#include <string>
#include <string.h>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <vector>
#include <csignal>
#include <cuda_runtime.h>
#include <io.h>

#if defined(_WIN32)||defined(_WIN64)
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

std::atomic<bool> Pause(false);
std::atomic<bool> Paused(false);
std::atomic<bool> stopMonitorKey(false);
int    idxcount  = 0;
double t_Paused  = 0.0;
bool   backupMode= false;
using namespace std;

VanitySearch* g_vs = nullptr;
std::atomic<bool> g_shutdown(false);

void signalHandler(int sig) {
    if (!backupMode) { printf("\n"); fflush(stdout); exit(sig); }
    if (g_shutdown.exchange(true)) exit(sig);
    printf("\n[!] Ctrl+C - shutting down..."); fflush(stdout);
    if (g_vs) g_vs->endOfSearch = true;
}

#if defined(_WIN32)||defined(_WIN64)
void monitorKeypress() {
    while (!stopMonitorKey) {
        Timer::SleepMillis(1);
        if (_kbhit()) {
            char ch = _getch();
            if (ch=='p'||ch=='P') { Pause = !Pause.load(); }
        }
    }
}
#else
// Linux keyboard monitor (simplified)
void monitorKeypress() {
    while (!stopMonitorKey) { Timer::SleepMillis(50); }
}
#endif

static void printFaq() {
    printf("\n");
    printf("=======================================================================\n");
    printf("  EKey-Jerboa V1.0.0 -- FULL FAQ & MANUAL\n");
    printf("=======================================================================\n\n");

    printf("1. WHAT THE PROGRAM DOES\n");
    printf("   EKey-Jerboa searches a chosen key range on the GPU for the private\n");
    printf("   key of a target Bitcoin address or public key. The range (a\n");
    printf("   \"chunk\") is split into many equal \"slots\"; the program scans them\n");
    printf("   in random order, hopping between them on a timer, and reports the\n");
    printf("   key the instant it is found.\n\n");

    printf("2. OPTIONS\n");
    printf("   -a <addr>    Target P2PKH (compressed) Bitcoin address\n");
    printf("   -p <pubkey>  Target compressed public key\n");
    printf("   -s 0x<hex>   Chunk start        -e 0x<hex>  Chunk end\n");
    printf("   -r <bits>    Bit-range shorthand (e.g. -r 71)\n");
    printf("   -T <sec>     Jump interval (default 30). 999999999 = full scan\n");
    printf("   -G <id>      GPU device id (default 0)\n");
    printf("   -W <0-7>     Grid: 0=auto  5=6144x256  7=12288x256\n");
    printf("   -b           Save/Resume progress\n");
    printf("   -D4..-D6     Split deeper (OFF/default = -D3)\n");
    printf("   -faq / -inf  This manual / version & credits\n\n");

    printf("3. FEATURES & HOW THEY WORK\n");
    printf("   - Target match: each GPU key is turned into a public key, hashed\n");
    printf("     (SHA256 then RIPEMD160) and compared with the target hash.\n");
    printf("   - Deep slot split (-D): the chunk is split by the Nth hex symbol\n");
    printf("     into equal slots (see section 4).\n");
    printf("   - Random slot order: slots are visited through a full-period LCG\n");
    printf("     permutation -- every slot exactly once per pass, pseudo-random.\n");
    printf("   - Time jumps (-T): every T seconds the current slot position is\n");
    printf("     saved and the program hops to the next slot. With -T 999999999\n");
    printf("     it reads each slot to the end before moving on.\n");
    printf("   - Resume & honest completion (-b): each slot's progress is stored,\n");
    printf("     so a resumed run continues where it stopped and the search ends\n");
    printf("     only when every slot has been fully read.\n");
    printf("   - Live console: current slot, hop counter, completed slots, keys\n");
    printf("     this hop, slot progress bar, speed and time to the next hop.\n\n");

    printf("4. SLOT SPLIT (-D)\n");
    printf("   The selected chunk (a range of 2nd-symbol slots) is split by the\n");
    printf("   Nth hex symbol. Slot count = (#selected slots) * 16^(N-2):\n");
    printf("     OFF/-D3 -> x16   -D4 -> x256   -D5 -> x4096   -D6 -> x65536\n");
    printf("   Example: 2 slots selected + OFF -> 32 slots.\n");
    printf("   Progress folder: d{N}_{puz}_0x{lo8}-0x{hi8}_W{grid}_rnd/\n");
    printf("   Different flags = different folders = no conflicts.\n\n");

    printf("5. OPTIMIZATION / PERFORMANCE\n");
    printf("   - SINGLE_TARGET_MODE: the single target hash is kept in GPU\n");
    printf("     constant memory and compared inline (no bloom/list lookups).\n");
    printf("   - Batched group EC: points are advanced in groups with ONE modular\n");
    printf("     inversion per group (batch/Montgomery inverse) instead of one\n");
    printf("     per key -- the dominant cost is amortised across the group.\n");
    printf("   - In-place SHA256 and force-inlined RIPEMD160 on the GPU.\n");
    printf("   - Endomorphism / curve symmetry from the VanitySearch core.\n");
    printf("   - Coalesced memory access and a tunable thread grid (-W).\n");
    printf("   - CPU-side hashing uses SSE; the build enables SSE/ADX/BMI.\n");
    printf("   Measured: ~1.47 Gk/s on an RTX A4000 (sm_86), CUDA 13.1.\n\n");

    printf("6. BUILDING\n");
    printf("   Windows: a ready compile_modern.bat is in the repository -- just\n");
    printf("            run it (needs CUDA Toolkit + Visual Studio build tools).\n");
    printf("            It produces EKey-Jerboa.exe.\n");
    printf("   Linux:   a Makefile is provided. From the project root run:\n");
    printf("              make            (or: make -j$(nproc))\n");
    printf("            Needs the CUDA Toolkit (nvcc). Override if needed:\n");
    printf("              make GENCODE=\"-gencode arch=compute_86,code=sm_86\"\n");
    printf("              make CUDA_PATH=/usr/local/cuda-13.1\n");
    printf("            The sources are cross-platform -- nothing to edit.\n\n");

    printf("7. GPU ARCHITECTURES\n");
    printf("   The sources/build target current NVIDIA architectures out of the\n");
    printf("   box: sm_75 (RTX 20xx), sm_86 (RTX 30xx / A4000), sm_89 (RTX 40xx)\n");
    printf("   plus compute_89 PTX so newer cards (RTX 50xx+) run via JIT.\n");
    printf("   Edit GENCODE / -arch to match your card exactly.\n\n");

    printf("8. LAUNCHER (GUI)\n");
    printf("   A graphical launcher builds the command line for you:\n");
    printf("   - pick puzzle / address, set the chunk on a visual ruler or in hex\n");
    printf("   - choose grid (-W), GPU id, jump interval (-T), split level (-D)\n");
    printf("   - Progress checkbox (-b) to save/resume; Save-as-Default and\n");
    printf("     Load-Progress to restore a previous session\n");
    printf("   - live alert when the chosen slots exceed the resume capacity\n");
    printf("   The launcher only starts EKey-Jerboa with the right flags -- all\n");
    printf("   GPU work is done by the compiled program itself.\n\n");

    printf("9. LAUNCHER SAFETY / SOURCE\n");
    printf("   The launcher is shipped compiled to an .exe to protect authorship.\n");
    printf("   It contains NO viruses and no hidden code. If you would rather not\n");
    printf("   run the .exe, you can ignore it completely and start EKey-Jerboa\n");
    printf("   directly from CMD using the flags in section 2 -- the launcher is\n");
    printf("   only a convenience front-end, nothing more.\n\n");

    printf("10. KEYBOARD\n");
    printf("   P = Pause/Resume     Ctrl+C = stop (saves first when -b is on)\n\n");

    printf("11. CONTACT\n");
    printf("   Questions / feedback: egor.gr1@gmail.com\n\n");

    printf("=======================================================================\n\n");
    exit(0);
}

static void printInfo() {
    printf("\n");
    printf("=======================================================================\n");
    printf("  EKey-Jerboa V1.0.0 -- Version Info & Credits\n");
    printf("=======================================================================\n\n");
    printf("  WHAT IT IS\n");
    printf("  EKey-Jerboa is a GPU-accelerated search tool for the Bitcoin\n");
    printf("  \"puzzle\" challenges. Given a target P2PKH address (or compressed\n");
    printf("  public key) and a key range, it sweeps that range on the GPU\n");
    printf("  looking for the matching private key. The range is split into\n");
    printf("  equal \"slots\" that the program hops across in random order,\n");
    printf("  saving progress so a search can be paused and resumed.\n\n");
    printf("  AUTHORSHIP\n");
    printf("  Programmed by Claude (Anthropic) -- model Claude Opus 4.8 --\n");
    printf("  under the meticulous guidance and direction of egorrushka, who\n");
    printf("  designed the engine behaviour, drove the architecture and tested\n");
    printf("  every single build.\n\n");
    printf("  BASED ON\n");
    printf("  The respected sources of VanitySearch by Jean Luc PONS (GPLv3).\n");
    printf("  https://github.com/JeanLucPons/VanitySearch\n");
    printf("  Fork author: egorrushka.\n\n");
    printf("  MADE IN\n");
    printf("  Written in Ukraine, under the extreme conditions of war, in the\n");
    printf("  glorious city of Chernihiv.\n\n");
    printf("  CONTACT\n");
    printf("  Feedback and questions: egor.gr1@gmail.com\n\n");
    printf("  License : GPLv3\n");
    printf("  GPU     : CUDA C++ (NVIDIA), sm_75/sm_86/sm_89 + PTX fallback\n");
    printf("  Tested  : RTX A4000 (sm_86), CUDA 13.1 -- ~1.47 Gk/s\n\n");
    printf("=======================================================================\n\n");
    exit(0);
}

static void printHelp() {
    printf("\nEKey-Jerboa V1.0.0  by egorrushka\n");
    printf("Based on VanitySearch by Jean Luc PONS\n\n");
    printf("Usage: EKey-Jerboa.exe -a <address> -s 0xSTART -e 0xEND [options]\n\n");
    printf("Required:\n");
    printf("  -a <addr>   Target Bitcoin P2PKH address\n");
    printf("  -p <pubkey> Target public key (compressed hex, alternative to -a)\n");
    printf("  -s <hex>    Chunk start (hex, e.g. 0x400000000000000000)\n");
    printf("  -e <hex>    Chunk end   (hex, e.g. 0x7fffffffffffffffff)\n");
    printf("  -r <bits>   Bit range (alternative to -s/-e)\n\n");
    printf("Options:\n");
    printf("  -T <sec>    Jump interval in seconds (default 30)\n");
    printf("  -W <0-10>   Grid profile  0=auto 7=6144x256 (A4000 default)\n");
    printf("  -G <id>     GPU device ID (default 0)\n");
    printf("  -b          Resume mode\n");
    printf("  -D4..-D6    Split deeper (OFF/default = -D3)\n");
    printf("  -faq        Full manual\n");
    printf("  -inf        Version and credits\n");
    printf("  -h          This help\n\n");
    printf("Grid profiles (-W):\n");
    printf("  0:auto  1:512  2:1024  3:2048  4:4096  5:6144  6:8192  7:12288\n\n");
    exit(0);
}

static const int GRIDS[] = {-1,512,1024,2048,4096,6144,8192,12288};
static const int NGRID   = 8;

static bool parseHex(const string& raw, Int& out) {
    string s = raw;
    if (s.size()>=2&&s[0]=='0'&&(s[1]=='x'||s[1]=='X')) s=s.substr(2);
    if (s.empty()||s.size()>64) return false;
    for (char c:s) if (!isxdigit((unsigned char)c)) return false;
    while (s.size()<64) s.insert(s.begin(),'0');
    vector<char> b(s.begin(),s.end()); b.push_back('\0');
    out.SetBase16(b.data()); return true;
}

int main(int argc, char* argv[]) {
    signal(SIGINT, signalHandler);
    thread inputThread(monitorKeypress);
    Timer::Init();
    setvbuf(stdout, NULL, _IONBF, 0); // disable CRT buffering: Win32 console API must stay in sync with printf
    Secp256K1* secp = new Secp256K1(); secp->Init();

    if (argc < 2) printHelp();

    string taddr, tpubkey, hexStart, hexEnd;
    int gpuId=0, bits=0, gridProfile=0;
    double jumpSec = 30.0;
    int  deepMode = 0;
    string launcherJson;  // launcher JSON (optional, from GUI)

    for (int i=1; i<argc; i++) {
        string arg = argv[i];
        if      (arg=="-h"||arg=="--help") printHelp();
        else if (arg=="-b") backupMode=true;
        else if (arg=="-a"&&i+1<argc) taddr    = argv[++i];
        else if (arg=="-p"&&i+1<argc) tpubkey  = argv[++i];
        else if (arg=="-s"&&i+1<argc) hexStart  = argv[++i];
        else if (arg=="-e"&&i+1<argc) hexEnd    = argv[++i];
        else if (arg=="-r"&&i+1<argc) bits = atoi(argv[++i]);
        else if (arg=="-T"&&i+1<argc) jumpSec = atof(argv[++i]);
        else if (arg=="-R") { /* order is always Random now — accepted, no-op */ }
        else if (arg.size()==3 && arg[0]=='-' &&
                 (arg[1]=='D'||arg[1]=='d') &&
                 arg[2]>='3' && arg[2]<='6')
            deepMode = arg[2] - '0';
        else if (arg.size()==3 && arg[0]=='-' &&
                 (arg[1]=='D'||arg[1]=='d') &&
                 arg[2]>='7' && arg[2]<='8') {
            fprintf(stderr,"[ERROR] -D: max level is 6 (per-slot resume cap ~4.2M slots)\n");
            exit(-1);
        }
        else if (arg=="-faq") printFaq();
        else if (arg=="-inf") printInfo();
        else if (arg=="-J"&&i+1<argc) jumpSec = atof(argv[++i])*60.0; // minutes compat
        else if (arg=="-G"&&i+1<argc) gpuId = atoi(argv[++i]);
        else if (arg=="-W"&&i+1<argc) {
            gridProfile = atoi(argv[++i]);
            if (gridProfile<0||gridProfile>=NGRID){fprintf(stderr,"[ERROR] -W: 0-%d\n",NGRID-1);exit(-1);}
        }
        else if (arg=="--launcher-file"&&i+1<argc) {
            // ?????? JSON ?? ?????????? ????? (??????? ???????? ????????????? ? cmd.exe)
            std::string lfpath = argv[++i];
            FILE* lf = fopen(lfpath.c_str(), "r");
            if (lf) {
                fseek(lf, 0, SEEK_END); long sz = ftell(lf); rewind(lf);
                if (sz > 0 && sz < 4096) {
                    launcherJson.resize(sz);
                    fread(&launcherJson[0], 1, sz, lf);
                    // trim trailing whitespace/newlines
                    while (!launcherJson.empty() &&
                           (launcherJson.back()=='\n'||launcherJson.back()=='\r'||launcherJson.back()==' '))
                        launcherJson.pop_back();
                }
                fclose(lf);
            }
        }
        else { fprintf(stderr,"[ERROR] Unknown: %s\n",arg.c_str()); printHelp(); }
    }

    if (taddr.empty()&&tpubkey.empty()){fprintf(stderr,"[ERROR] Need -a or -p\n");printHelp();}
    bool useChunk = (!hexStart.empty()||!hexEnd.empty());
    if (useChunk&&(hexStart.empty()||hexEnd.empty())){fprintf(stderr,"[ERROR] Need both -s and -e\n");exit(-1);}
    if (!useChunk&&bits==0){fprintf(stderr,"[ERROR] Need -s/-e or -r\n");printHelp();}
    if (jumpSec<1.0) jumpSec=1.0;
    if (deepMode==0) deepMode=3;   // default: split by 2nd symbol (16 slots/top) = -D3

    // Enable ANSI + set fixed console window size (Windows)
#if defined(_WIN32)||defined(_WIN64)
    {
        HANDLE h=GetStdHandle(STD_OUTPUT_HANDLE);
        DWORD m=0;
        if(GetConsoleMode(h,&m))
            SetConsoleMode(h,m|ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        // Fixed window: 190 cols x 40 rows  (wide enough for 8-sector table)
        COORD buf={190,2000};
        SetConsoleScreenBufferSize(h,buf);
        SMALL_RECT win={0,0,189,39};
        SetConsoleWindowInfo(h,TRUE,&win);
        // Set window title
        SetConsoleTitleA("EKey-Jerboa V1.0.0  by egorrushka");
    }
#endif

    // CUDA check
    int devCount=0;
    cudaGetDeviceCount(&devCount);
    if (devCount==0){fprintf(stderr,"[ERROR] No CUDA GPU found\n");exit(-1);}
    if (gpuId>=devCount||gpuId<0){fprintf(stderr,"[ERROR] Invalid GPU id %d\n",gpuId);exit(-1);}

    // Build params
    BITCRACK_PARAM bc={};
    if (useChunk) {
        if (!parseHex(hexStart,bc.ksStart)||!parseHex(hexEnd,bc.ksFinish)){fprintf(stderr,"[ERROR] Bad hex\n");exit(-1);}
        if (bc.ksFinish.IsLower(&bc.ksStart)){fprintf(stderr,"[ERROR] end < start\n");exit(-1);}
    } else {
        bc.ksStart.SetInt32(1); if(bits>1) bc.ksStart.ShiftL(bits-1);
        bc.ksFinish.SetInt32(1); bc.ksFinish.ShiftL(bits); bc.ksFinish.SubOne();
    }
    bc.ksNext.Set(&bc.ksStart);
    // Order is always Random (LCG full-period). Sequential mode removed.
    bc.randSlotMode  = true;
    bc.jerboaJumpSec = jumpSec;
    // Legacy COMB fields unused but zeroed
    bc.combMode=false; bc.combSequential=false; bc.combSlotsCount=0;
    bc.combDone=false; bc.combJumpMinutes=0; bc.combInterleaveStep=0;
    bc.combCycleNum=0; bc.combCycleTotal=0; bc.combCurrentPass=0;
    bc.combCoverage.SetInt32(0); bc.combBaseOffset.SetInt32(0);
    bc.deepMode      = deepMode;
    bc.gridProfile   = gridProfile;
    // Store launcher JSON string (from --launcher "...") for satellite file
    memset(bc.launcherJson, 0, sizeof(bc.launcherJson));
    if (!launcherJson.empty())
        strncpy(bc.launcherJson, launcherJson.c_str(), sizeof(bc.launcherJson)-1);

    // Print header
    printf("\n[+] EKey-Jerboa V1.0.0  by egorrushka\n");
    if (!tpubkey.empty())
        printf("[+] Search : %s [Public Key]\n", tpubkey.c_str());
    else
        printf("[+] Search : %s [P2PKH/Compressed]\n", taddr.c_str());
    time_t now=time(NULL);
    char tbuf[64]; ctime_s(tbuf,sizeof(tbuf),&now);
    printf("[+] Start  : %s", tbuf);
    {
        string cs = bc.ksStart.GetBase16();
        string ce = bc.ksFinish.GetBase16();
        // trim leading zeros
        size_t n=cs.find_first_not_of('0'); if(n!=string::npos)cs=cs.substr(n);else cs="0";
        n=ce.find_first_not_of('0'); if(n!=string::npos)ce=ce.substr(n);else ce="0";
        printf("[+] Chunk  : 0x%s -> 0x%s\n", cs.c_str(), ce.c_str());
    }
    // Jump interval display: show "No Jump" when T=999999999
    if (jumpSec >= 999999998.0)
        printf("[+] Jump   : No Jump (sequential scan)\n");
    else
        printf("[+] Jump   : %.1f sec/slot\n", jumpSec);
    printf("[+] Mode   : Deep D%d  (Random)\n", deepMode);
    fflush(stdout);

    vector<string> targets;
    if (!tpubkey.empty()) targets.push_back(tpubkey);
    else targets.push_back(taddr);

    uint32_t maxFound = 65536*4;
    VanitySearch* v = new VanitySearch(secp, targets, SEARCH_COMPRESSED, true, "", maxFound, &bc);
    g_vs = v;

    int gx = (gridProfile>0&&gridProfile<NGRID) ? GRIDS[gridProfile] : 6144;
    vector<int> gpuIds={gpuId};
    vector<int> gridSizes={gx,256};
    if (gx>0)
        printf("[+] Grid   : %d x 256 = %d threads\n", gx, gx*256);
    fflush(stdout);

    v->Search(gpuIds, gridSizes);

    stopMonitorKey=true;
    if (inputThread.joinable()) inputThread.join();
    printf("\n");
    delete v; delete secp;
    return 0;
}
