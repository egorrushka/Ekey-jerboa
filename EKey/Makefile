# ============================================================================
#  EKey-Jerboa  —  Linux / CUDA Makefile
#  Author fork: egorrushka   |   Based on VanitySearch (Jean Luc PONS, GPLv3)
#
#  Build:   make                 (uses GPU archs below)
#           make -j$(nproc)       (parallel build)
#  Clean:   make clean
#  Custom:  make CUDA_ARCH=sm_86  GENCODE="-gencode arch=compute_86,code=sm_86"
#           make CUDA_PATH=/usr/local/cuda-13.1
#
#  Run from the project root (where main.cpp and the hash/ and GPU/ dirs are).
#
#  IMPORTANT: WIN64 is intentionally NOT defined. The sources are cross-platform
#  via #ifdef WIN64 / #ifndef WIN64; leaving WIN64 undefined selects the Linux
#  code paths (gettimeofday, usleep, pthread, __builtin/asm intrinsics, etc.).
# ============================================================================

# ── Toolchain ───────────────────────────────────────────────────────────────
NVCC      ?= nvcc
CUDA_PATH ?= /usr/local/cuda
TARGET    ?= EKey-Jerboa
STD       ?= -std=c++17

# ── GPU architectures (edit to match your card) ─────────────────────────────
#   sm_75 : RTX 2060–2080 Ti        sm_86 : RTX 3070–3090 / A4000
#   sm_89 : RTX 4070–4090           compute_89 PTX : RTX 50xx+ (JIT)
GENCODE ?= -gencode arch=compute_75,code=sm_75 \
           -gencode arch=compute_86,code=sm_86 \
           -gencode arch=compute_89,code=sm_89 \
           -gencode arch=compute_89,code=compute_89

# ── Host (g++) flags ─────────────────────────────────────────────────────────
#   -march=native enables SSE/ADX/BMI used by Int.cpp and hash/*_sse.cpp.
#   For a portable binary replace it with e.g. -msse4.2 -mavx2 -mbmi2 -madx.
HOSTFLAGS ?= -O2 -march=native -funroll-loops -fopenmp -pthread \
             -fno-strict-aliasing -Wno-unused-result -Wno-write-strings

# ── nvcc flags ───────────────────────────────────────────────────────────────
NVCCFLAGS = -O3 -m64 -DWITHGPU -I. $(STD) $(GENCODE) \
            -Xcompiler "$(HOSTFLAGS)"
LDFLAGS   = -Xcompiler "-fopenmp -pthread"

# ── Sources (same set as compile_modern.bat) ────────────────────────────────
SRC_CPP = main.cpp Vanity.cpp Jerboa.cpp \
          Int.cpp IntMod.cpp IntGroup.cpp \
          Point.cpp SECP256K1.cpp \
          Base58.cpp Bech32.cpp \
          Timer.cpp Random.cpp Wildcard.cpp \
          hash/sha256.cpp hash/sha256_sse.cpp \
          hash/ripemd160.cpp hash/ripemd160_sse.cpp \
          hash/sha512.cpp \
          GPU/GPUGenerate.cpp
SRC_CU  = GPU/GPUEngine.cu

OBJDIR = obj
OBJ    = $(addprefix $(OBJDIR)/,$(SRC_CPP:.cpp=.o)) \
         $(addprefix $(OBJDIR)/,$(SRC_CU:.cu=.o))

# ── Rules ────────────────────────────────────────────────────────────────────
.PHONY: all clean info
all: $(TARGET)

$(TARGET): $(OBJ)
	$(NVCC) $(GENCODE) -m64 $(OBJ) $(LDFLAGS) -o $@
	@echo ""
	@echo "[OK] $(TARGET) built."
	@echo "     Run: ./$(TARGET) -faq   for usage."

$(OBJDIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.cu
	@mkdir -p $(dir $@)
	$(NVCC) $(NVCCFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJDIR) $(TARGET)
	@echo "[clean] done"

info:
	@echo "NVCC      = $(NVCC)"
	@echo "CUDA_PATH = $(CUDA_PATH)"
	@echo "TARGET    = $(TARGET)"
	@echo "GENCODE   = $(GENCODE)"
	@echo "HOSTFLAGS = $(HOSTFLAGS)"
