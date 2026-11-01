// Host-side check of the real-avatar path: index a cache folder, detect the
// user's avatar, build the guest assets and print a summary.
// usage: avatar_test <cache dir> [coordinate_system]
#include <cstdio>
#include <cstdlib>

#include "avatar_real.h"
#include "xenia/avatars/guest_asset.h"

using namespace dcc_avatar;
using namespace xe::avatars;

static uint32_t BE(const uint8_t* p) { return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

int main(int argc, char** argv) {
  AssetCache cache;
  if (!cache.Open(argv[1])) { printf("no assets\n"); return 1; }
  printf("indexed %zu assets\n", cache.size());
  X_AVATAR_METADATA md;
  std::vector<AssetId> extra;
  std::string desc;
  if (!cache.DetectUserAvatar(md, extra, desc)) { printf("detect failed: %s\n", desc.c_str()); return 1; }
  printf("detected: %s\n", desc.c_str());
  BuiltAssets b;
  uint32_t coord = argc > 2 ? atoi(argv[2]) : 0;
  if (!BuildAssets(cache, md, extra, 0x1fff, coord, 2, b)) { printf("build failed\n"); return 1; }
  printf("cpu %zu gpu %zu relocs %zu+%zu\n", b.cpu.size(), b.gpu.size(), b.cpu_relocs.size(), b.gpu_relocs.size());
  const uint8_t* c = b.cpu.data();
  uint32_t skel = BE(c), ncomp = BE(c + 8), infos = BE(c + 12), models = BE(c + 16);
  printf("skeleton@%x joints %u, components %u\n", skel, BE(c + skel), ncomp);
  for (uint32_t i = 0; i < ncomp; ++i) {
    const uint8_t* m = c + models + 52 * i;
    const uint8_t* inf = c + infos + 32 * i;
    printf(" comp %u cat %04x: cpu %u gpu %u batches %u textures %u\n", i, (inf[16] << 8) | inf[17],
           BE(m), BE(m + 4), BE(m + 20), BE(m + 24));
    uint32_t batches = BE(m + 44), texs = BE(m + 48);
    for (uint32_t j = 0; j < BE(m + 20); ++j) {
      const uint8_t* bt = c + batches + 512 * j;
      const uint8_t* tail = bt + 4 + 480;
      printf("   batch %u shader %u tris %u verts %u uv %u vsize %u params:", j, BE(bt), BE(tail), BE(tail + 4), BE(tail + 8), BE(tail + 12));
      for (int k = 0; k < 20; ++k) {
        const uint8_t* p = bt + 4 + 24 * k;
        if (BE(p)) printf(" %u/%u", BE(p), BE(p + 4));
      }
      printf("\n");
    }
    for (uint32_t j = 0; j < BE(m + 24); ++j) {
      const uint8_t* t = c + texs + 44 * j;
      printf("   tex %u fmt %08x %ux%u size %u layers %u\n", j, BE(t), BE(t + 4), BE(t + 8), BE(t + 12), BE(t + 32));
    }
  }
  FILE* f = fopen("built.bin", "wb");
  fwrite(b.cpu.data(), 1, b.cpu.size(), f); fwrite(b.gpu.data(), 1, b.gpu.size(), f); fclose(f);
  return 0;
}
