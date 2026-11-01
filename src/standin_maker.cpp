// Builds the stand-in avatar (the contents of game/standin_avatar.bin) from
// the game's own data.pak at startup, so it doesn't have to be shipped.
// C++ port of tools/make_standin_avatar.py; see that script for the format
// and the reasoning behind the layout and shader parameters.

#include "standin_maker.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <unordered_map>

#include <rex/logging.h>

extern "C" {
#include "third_party/lzma/LzmaDec.h"
}

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_SIMD  // match PIL's output as closely as the decoder allows
#include "third_party/stb/stb_image.h"

namespace dcc_avatar {
namespace {

const char* const kJointToCalBone[] = {
    "BASE", "BACKA", "LF_H", "RT_H", "SC_BASE", "BACKB", "LF_K", "LF_SC_H",
    "RT_K", "RT_SC_H", "SC_BACKA", "LF_A", "LF_C", "LF_SC_K", "NECK", "RT_A",
    "RT_C", "RT_SC_K", "SC_BACKB", "HEAD", "LF_S", "LF_T", "RT_S", "RT_T",
    "SC_NECK", "LF_E", "LF_SC_S", "LF_SC_S_SKIN", "RT_E", "RT_SC_S",
    "RT_SC_S_SKIN", "LF_E_SKIN", "LF_SC_E", "LF_W", "RT_E_SKIN", "RT_SC_E",
    "RT_W", "LF_FINGA", "LF_FINGB", "LF_FINGC", "LF_FINGD", "LF_PROP",
    "LF_SPECIAL", "LF_THUMB", "RT_FINGA", "RT_FINGB", "RT_FINGC", "RT_FINGD",
    "RT_PROP", "RT_SPECIAL", "RT_THUMB", "LF_FINGA1", "LF_FINGB1", "LF_FINGC1",
    "LF_FINGD1", "LF_THUMB1", "RT_FINGA1", "RT_FINGB1", "RT_FINGC1",
    "RT_FINGD1", "RT_THUMB1", "LF_FINGA2", "LF_FINGB2", "LF_FINGC2",
    "LF_FINGD2", "LF_THUMB2", "RT_FINGA2", "RT_FINGB2", "RT_FINGC2",
    "RT_FINGD2", "RT_THUMB2",
};
constexpr int kJoints = int(std::size(kJointToCalBone));

constexpr uint32_t kUvCount = 3;
constexpr uint32_t kVertexSize = 32 + 4 * (kUvCount - 1);
constexpr uint32_t kShaderId = 0;
constexpr uint32_t kTexFormatArgb8 = 0x18280086;
constexpr uint16_t kCategoryBody = 1 << 1;
const std::map<int, uint32_t> kSolidColours = {
    {0, 0xFF8A3B32}, {1, 0xFF2A1E18}, {2, 0xFF2A1E18}, {3, 0xFF4A2E1A}, {4, 0xFF4A2E1A},
};

// ---- little-endian readers for the pak and Cal3D files ----
struct Reader {
  const uint8_t* d;
  size_t n, p = 0;
  bool ok = true;
  bool need(size_t k) {
    if (p + k > n) ok = false;
    return ok;
  }
  uint32_t u32() {
    if (!need(4)) return 0;
    uint32_t v;
    std::memcpy(&v, d + p, 4);  // host is little-endian (x86-64 / arm64)
    p += 4;
    return v;
  }
  int32_t i32() { return int32_t(u32()); }
  float f32() {
    uint32_t u = u32();
    float f;
    std::memcpy(&f, &u, 4);
    return f;
  }
  void skip(size_t k) {
    if (need(k)) p += k;
  }
};

void* LzmaAlloc(ISzAllocPtr, size_t size) { return std::malloc(size); }
void LzmaFree(ISzAllocPtr, void* p) { std::free(p); }
const ISzAlloc kAlloc = {LzmaAlloc, LzmaFree};

bool ReadPakFile(const std::vector<uint8_t>& pak, const std::string& want,
                 std::vector<uint8_t>& out) {
  Reader r{pak.data(), pak.size(), 18};
  uint32_t count = r.u32();
  r.skip(4);
  auto lower = [](std::string s) {
    for (auto& c : s) c = char(std::tolower(uint8_t(c)));
    return s;
  };
  for (uint32_t i = 0; i < count && r.ok; ++i) {
    uint32_t off = r.u32(), size = r.u32();
    r.skip(4);
    uint32_t len = r.u32();
    if (!r.need(len)) return false;
    std::string name(reinterpret_cast<const char*>(pak.data() + r.p), len);
    r.skip(len);
    if (lower(name) != want) continue;
    if (uint64_t(off) + size > pak.size() || size < 13) return false;
    const uint8_t* blob = pak.data() + off;
    uint64_t out_size;
    std::memcpy(&out_size, blob + 5, 8);
    if (out_size > (64u << 20)) return false;
    out.resize(size_t(out_size));
    SizeT dest_len = out.size(), src_len = size - 13;
    ELzmaStatus status;
    SRes res = LzmaDecode(out.data(), &dest_len, blob + 13, &src_len, blob, 5,
                          LZMA_FINISH_ANY, &status, &kAlloc);
    return res == SZ_OK && dest_len == out.size();
  }
  return false;
}

struct Bone {
  std::string name;
  int parent;
  double lt[3], lr[4];
};

bool LoadCsf(const std::vector<uint8_t>& d, std::vector<Bone>& bones) {
  Reader r{d.data(), d.size(), 8};
  uint32_t n = r.u32();
  for (uint32_t i = 0; i < n && r.ok; ++i) {
    uint32_t ln = r.u32();
    if (!r.need(ln)) return false;
    Bone b;
    b.name.assign(reinterpret_cast<const char*>(d.data() + r.p), ln);
    b.name.erase(std::find(b.name.begin(), b.name.end(), '\0'), b.name.end());
    r.skip(ln);
    r.skip(28);
    for (double& v : b.lt) v = r.f32();
    for (double& v : b.lr) v = r.f32();
    b.parent = r.i32();
    int nchild = r.i32();
    r.skip(4 * size_t(std::max(nchild, 0)));
    bones.push_back(b);
  }
  return r.ok;
}

struct Vertex {
  double pos[3], nrm[3], uv[2];
  std::vector<std::pair<int, double>> infl;
};
struct Sub {
  std::vector<Vertex> verts;
  std::vector<std::array<int, 3>> faces;
};

bool LoadCmf(const std::vector<uint8_t>& d, std::vector<Sub>& subs) {
  Reader r{d.data(), d.size(), 12};
  uint32_t nsub = r.u32();
  for (uint32_t s = 0; s < nsub && r.ok; ++s) {
    r.skip(4);
    int nv = r.i32(), nf = r.i32();
    r.skip(4);
    int nspr = r.i32(), nmap = r.i32();
    if (nv < 0 || nf < 0 || nmap < 0 || nspr < 0) return false;
    Sub sub;
    for (int i = 0; i < nv && r.ok; ++i) {
      Vertex v{};
      for (double& c : v.pos) c = r.f32();
      for (double& c : v.nrm) c = r.f32();
      r.skip(8);
      for (int k = 0; k < nmap; ++k) {
        double u = r.f32(), vv = r.f32();
        if (k == 0) v.uv[0] = u, v.uv[1] = vv;
      }
      int ninf = r.i32();
      for (int k = 0; k < ninf && r.ok; ++k) {
        int b = r.i32();
        double w = r.f32();
        v.infl.push_back({b, w});
      }
      if (nspr) r.skip(4);
      sub.verts.push_back(std::move(v));
    }
    r.skip(size_t(nspr) * 24);
    for (int i = 0; i < nf && r.ok; ++i) {
      int a = r.i32(), b = r.i32(), c = r.i32();
      sub.faces.push_back({a, b, c});
    }
    subs.push_back(std::move(sub));
  }
  return r.ok && r.p == d.size();
}

struct Vec3 {
  double x, y, z;
};

Vec3 QRotInv(const double q[4], Vec3 v) {
  double x = -q[0], y = -q[1], z = -q[2], w = q[3];
  double tx = 2 * (y * v.z - z * v.y), ty = 2 * (z * v.x - x * v.z), tz = 2 * (x * v.y - y * v.x);
  return {v.x + w * tx + (y * tz - z * ty), v.y + w * ty + (z * tx - x * tz),
          v.z + w * tz + (x * ty - y * tx)};
}

Vec3 ToAvatar(const double v[3], bool flip_z) {
  return {v[0], v[2], flip_z ? v[1] : -v[1]};
}

// Python's round(): half to even (the default floating-point rounding mode).
int RoundEven(double v) { return int(std::nearbyint(v)); }

uint16_t F16(double value) {
  // IEEE half, round to nearest even (as struct.pack('>e')).
  float f = float(value);
  uint32_t x;
  std::memcpy(&x, &f, 4);
  uint32_t sign = (x >> 16) & 0x8000;
  int32_t exp = int32_t((x >> 23) & 0xFF) - 127 + 15;
  uint32_t mant = x & 0x7FFFFF;
  if (((x >> 23) & 0xFF) == 0xFF) return uint16_t(sign | 0x7C00 | (mant ? 0x200 : 0));
  // Python packs the double directly; for the values here (UVs) float is exact enough.
  if (exp >= 31) return uint16_t(sign | 0x7C00);
  if (exp <= 0) {
    if (exp < -10) return uint16_t(sign);
    mant |= 0x800000;
    uint32_t shift = uint32_t(14 - exp);
    uint32_t half = mant >> shift, rem = mant & ((1u << shift) - 1), mid = 1u << (shift - 1);
    if (rem > mid || (rem == mid && (half & 1))) ++half;
    return uint16_t(sign | half);
  }
  uint32_t half = (uint32_t(exp) << 10) | (mant >> 13), rem = mant & 0x1FFF;
  if (rem > 0x1000 || (rem == 0x1000 && (half & 1))) ++half;
  return uint16_t(sign | half);
}

uint32_t PackHend3n(Vec3 n) {
  double l = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
  if (l == 0) l = 1.0;
  double x = n.x / l, y = n.y / l, z = n.z / l;
  auto c = [](double v) { return std::max(-1.0, std::min(1.0, v)); };
  uint32_t xi = uint32_t(RoundEven(c(x) * 1023)) & 0x7FF;
  uint32_t yi = uint32_t(RoundEven(c(y) * 1023)) & 0x7FF;
  uint32_t zi = uint32_t(RoundEven(c(z) * 511)) & 0x3FF;
  return xi | (yi << 11) | (zi << 22);
}

// ---- big-endian guest buffers ----
struct Buf {
  std::vector<uint8_t> b;
  uint32_t align;
  uint32_t Claim(size_t size, uint32_t a = 0) {
    if (!a) a = align;
    size_t off = (b.size() + a - 1) & ~size_t(a - 1);
    b.resize(off + size);
    return uint32_t(off);
  }
  void U32(uint32_t off, uint32_t v) {
    b[off] = uint8_t(v >> 24), b[off + 1] = uint8_t(v >> 16), b[off + 2] = uint8_t(v >> 8),
    b[off + 3] = uint8_t(v);
  }
  void U16(uint32_t off, uint16_t v) { b[off] = uint8_t(v >> 8), b[off + 1] = uint8_t(v); }
  void F32(uint32_t off, double v) {
    float f = float(v);
    uint32_t u;
    std::memcpy(&u, &f, 4);
    U32(off, u);
  }
};

void PushBE32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(uint8_t(v >> 24)), o.push_back(uint8_t(v >> 16)), o.push_back(uint8_t(v >> 8)),
      o.push_back(uint8_t(v));
}
void PushBE16(std::vector<uint8_t>& o, uint16_t v) {
  o.push_back(uint8_t(v >> 8)), o.push_back(uint8_t(v));
}

struct Texture {
  uint32_t w, h;
  std::vector<uint32_t> argb;  // row-major
};

struct Variant {
  Buf cpu{{}, 16}, gpu{{}, 4096};
  std::vector<uint32_t> cpu_relocs, gpu_relocs;
};

void BuildVariant(const std::vector<Bone>& bones, const std::vector<Sub>& subs,
                  const Texture& body_tex, bool flip_z, bool with_body, Variant& v,
                  const std::vector<int>& joint_cal, const std::unordered_map<int, int>& cal_to_avatar) {
  auto& cpu = v.cpu;
  auto& gpu = v.gpu;
  auto cptr = [&](uint32_t f, uint32_t t) { cpu.U32(f, t), v.cpu_relocs.push_back(f); };
  auto gptr = [&](uint32_t f, uint32_t t) { cpu.U32(f, t), v.gpu_relocs.push_back(f); };

  std::vector<Vec3> joint_pos(kJoints);
  std::vector<uint32_t> joint_parent(kJoints);
  for (int i = 0; i < kJoints; ++i) {
    const Bone& b = bones[joint_cal[i]];
    Vec3 world = QRotInv(b.lr, {-b.lt[0], -b.lt[1], -b.lt[2]});
    double w3[3] = {world.x, world.y, world.z};
    joint_pos[i] = ToAvatar(w3, flip_z);
    joint_parent[i] = b.parent < 0 ? 255 : uint32_t(cal_to_avatar.at(b.parent));
  }

  uint32_t assets = cpu.Claim(20);
  uint32_t skel = cpu.Claim(8);
  cptr(assets + 0, skel);
  uint32_t joints = cpu.Claim(96 * kJoints);
  cpu.U32(skel, kJoints);
  cptr(skel + 4, joints);
  std::vector<uint32_t> first_child(kJoints, 255), next_sib(kJoints, 255);
  for (int parent = 0; parent < kJoints; ++parent) {
    int prev = -1;
    for (int c = 0; c < kJoints; ++c) {
      if (joint_parent[c] != uint32_t(parent)) continue;
      if (prev < 0) first_child[parent] = c;
      else next_sib[prev] = c;
      prev = c;
    }
  }
  for (int i = 0; i < kJoints; ++i) {
    uint32_t o = joints + 96 * i;
    Vec3 p = joint_pos[i], l = p;
    if (joint_parent[i] != 255) {
      Vec3 q = joint_pos[joint_parent[i]];
      l = {p.x - q.x, p.y - q.y, p.z - q.z};
    }
    cpu.U32(o, joint_parent[i]), cpu.U32(o + 4, first_child[i]), cpu.U32(o + 8, next_sib[i]),
        cpu.U32(o + 12, 0);
    const double bind[8] = {p.x, p.y, p.z, 1, 0, 0, 0, 1};
    for (int k = 0; k < 8; ++k) cpu.F32(o + 16 + 4 * k, bind[k]);
    const double pose[12] = {l.x, l.y, l.z, 1, 0, 0, 0, 1, 1, 1, 1, 1};
    for (int k = 0; k < 12; ++k) cpu.F32(o + 48 + 4 * k, pose[k]);
  }

  if (!with_body) return;  // assets+4..12 stay zero

  uint32_t info = cpu.Claim(32);
  uint32_t model = cpu.Claim(52);
  cpu.U32(assets + 8, 1);
  cptr(assets + 12, info);
  cptr(assets + 16, model);
  // MALE_BODY_ASSET: >IHH8s (2, 0, 1, c1c8f109a19cb2e0)
  cpu.U32(info, 2), cpu.U16(info + 4, 0), cpu.U16(info + 6, 1);
  const uint8_t tail_id[8] = {0xC1, 0xC8, 0xF1, 0x09, 0xA1, 0x9C, 0xB2, 0xE0};
  std::memcpy(&cpu.b[info + 8], tail_id, 8);
  cpu.U16(info + 16, kCategoryBody);

  std::vector<Texture> tex;
  tex.push_back(body_tex);
  tex.push_back({128, 32, std::vector<uint32_t>(128 * 32, 0)});
  std::map<uint32_t, int> solid_index;
  for (auto& [sub, argb] : kSolidColours) {
    if (solid_index.count(argb)) continue;
    solid_index[argb] = int(tex.size());
    tex.push_back({128, 32, std::vector<uint32_t>(128 * 32, argb)});
  }

  uint32_t batch_count = uint32_t(subs.size());
  uint32_t model_cpu_start = cpu.Claim(512 * batch_count + 44 * tex.size(), 16);
  uint32_t batches = model_cpu_start;
  uint32_t textures = batches + 512 * batch_count;

  std::vector<std::vector<uint8_t>> vbs, ibs;
  for (const Sub& sub : subs) {
    std::vector<uint8_t> vb, ib;
    for (const Vertex& vx : sub.verts) {
      Vec3 pos = ToAvatar(vx.pos, flip_z), nrm = ToAvatar(vx.nrm, flip_z);
      auto infl = vx.infl;
      std::stable_sort(infl.begin(), infl.end(),
                       [](auto& a, auto& b) { return a.second > b.second; });
      if (infl.size() > 4) infl.resize(4);
      double tot = 0;
      for (auto& [b, w] : infl) tot += w;
      if (tot == 0) tot = 1.0;
      std::vector<uint32_t> idx, wts;
      for (auto& [b, w] : infl) {
        auto it = cal_to_avatar.find(b);
        idx.push_back(it == cal_to_avatar.end() ? 0 : uint32_t(it->second));
        wts.push_back(uint32_t(RoundEven(w / tot * 255)));
      }
      if (!wts.empty()) {
        int sum = 0;
        for (auto w : wts) sum += int(w);
        wts[0] = uint32_t(int(wts[0]) + 255 - sum);
      }
      while (idx.size() < 4) {
        idx.push_back(idx.empty() ? 0 : idx[0]);
        wts.push_back(0);
      }
      uint32_t bw = (wts[0] & 0xFF) | (wts[1] & 0xFF) << 8 | (wts[2] & 0xFF) << 16 | (wts[3] & 0xFF) << 24;
      uint32_t bi = (idx[0] & 0xFF) | (idx[1] & 0xFF) << 8 | (idx[2] & 0xFF) << 16 | (idx[3] & 0xFF) << 24;
      auto pf = [&](double d) {
        float f = float(d);
        uint32_t u;
        std::memcpy(&u, &f, 4);
        PushBE32(vb, u);
      };
      pf(pos.x), pf(pos.y), pf(pos.z);
      PushBE32(vb, PackHend3n(nrm));
      PushBE32(vb, bw), PushBE32(vb, bi), PushBE32(vb, 0xFFFFFFFF);
      for (uint32_t k = 0; k < kUvCount; ++k) PushBE16(vb, F16(vx.uv[0])), PushBE16(vb, F16(vx.uv[1]));
    }
    for (auto f : sub.faces) {
      if (flip_z) std::swap(f[1], f[2]);
      for (int k : f) PushBE16(ib, uint16_t(k));
    }
    vbs.push_back(std::move(vb));
    ibs.push_back(std::move(ib));
  }

  auto place = [&](std::vector<std::vector<uint8_t>>& parts, std::vector<uint32_t>& offs) {
    size_t total = 0;
    for (auto& p : parts) total += p.size();
    uint32_t start = gpu.Claim(total, 4096), o = start;
    for (auto& p : parts) {
      if (!p.empty()) std::memcpy(&gpu.b[o], p.data(), p.size());
      offs.push_back(o);
      o += uint32_t(p.size());
    }
    return std::pair{start, o - start};
  };
  std::vector<uint32_t> vertex_offsets, index_offsets;
  auto [vb_off, vb_size] = place(vbs, vertex_offsets);
  auto [ib_off, ib_size] = place(ibs, index_offsets);

  uint32_t tex_start = 0;
  std::vector<std::pair<uint32_t, uint32_t>> tex_data;
  for (size_t ti = 0; ti < tex.size(); ++ti) {
    const Texture& t = tex[ti];
    uint32_t pitch = (t.w * 4 + 511) & ~511u;
    uint32_t size = pitch * t.h;
    uint32_t off = gpu.Claim(size, 4096);
    if (ti == 0) tex_start = off;
    for (uint32_t y = 0; y < t.h; ++y)
      for (uint32_t x = 0; x < t.w; ++x) gpu.U32(off + y * pitch + x * 4, t.argb[y * t.w + x]);
    tex_data.push_back({off, size});
  }
  uint32_t tex_size = uint32_t(gpu.b.size()) - tex_start;

  for (size_t ti = 0; ti < tex.size(); ++ti) {
    uint32_t t = textures + 44 * uint32_t(ti);
    auto [doff, dsize] = tex_data[ti];
    const uint32_t vals[9] = {kTexFormatArgb8, tex[ti].w, tex[ti].h, dsize, 0, dsize, 0, 1, 1};
    for (int k = 0; k < 9; ++k) cpu.U32(t + 4 * k, vals[k]);
    gptr(t + 36, doff);
    cpu.U32(t + 40, 0);
  }

  for (uint32_t si = 0; si < batch_count; ++si) {
    uint32_t bo = batches + 512 * si;
    cpu.U32(bo, kShaderId);
    auto sc = kSolidColours.find(int(si));
    uint16_t colour_tex = sc == kSolidColours.end() ? 0 : uint16_t(solid_index[sc->second]);
    const std::pair<uint16_t, uint16_t> params[3] = {{colour_tex, 0}, {1, 1}, {1, 2}};
    for (uint32_t pi = 0; pi < 3; ++pi) {
      uint32_t po = bo + 4 + 24 * pi;
      cpu.U32(po, 1), cpu.U32(po + 4, pi + 1), cpu.U16(po + 8, params[pi].first),
          cpu.U16(po + 10, params[pi].second), cpu.U32(po + 12, 0);
    }
    const struct {
      uint32_t usage;
      double v[4];
    } consts[6] = {
        {22, {1, 1, 1, 1}}, {23, {1, 1, 1, 1}}, {24, {1, 1, 1, 1}},
        {25, {0, 0, 0, 0}}, {26, {0, 0, 0, 0}},
        {27, {0.54901963, 0.5019608, 0.40392157, 1.6}},
    };
    for (uint32_t ci = 0; ci < 6; ++ci) {
      uint32_t po = bo + 4 + 24 * (3 + ci);
      cpu.U32(po, 3), cpu.U32(po + 4, consts[ci].usage);
      for (int k = 0; k < 4; ++k) cpu.F32(po + 8 + 4 * k, consts[ci].v[k]);
    }
    uint32_t tail = bo + 4 + 24 * 20;
    const uint32_t tv[5] = {uint32_t(subs[si].faces.size()), uint32_t(subs[si].verts.size()),
                            kUvCount, kVertexSize, 2};
    for (int k = 0; k < 5; ++k) cpu.U32(tail + 4 * k, tv[k]);
    gptr(tail + 20, vertex_offsets[si]);
    gptr(tail + 24, index_offsets[si]);
  }

  uint32_t model_cpu_size = textures + 44 * uint32_t(tex.size()) - model_cpu_start;
  const uint32_t mv[7] = {model_cpu_size, uint32_t(gpu.b.size()), tex_size, vb_size, ib_size,
                          batch_count, uint32_t(tex.size())};
  for (int k = 0; k < 7; ++k) cpu.U32(model + 4 * k, mv[k]);
  cptr(model + 28, model_cpu_start);
  gptr(model + 32, 0);
  gptr(model + 36, vb_off);
  gptr(model + 40, ib_off);
  cptr(model + 44, batches);
  cptr(model + 48, textures);
}

void PushLE32(std::vector<uint8_t>& o, uint32_t v) {
  o.push_back(uint8_t(v)), o.push_back(uint8_t(v >> 8)), o.push_back(uint8_t(v >> 16)),
      o.push_back(uint8_t(v >> 24));
}

}  // namespace

bool MakeStandinFromPak(const std::filesystem::path& pak_path, std::vector<uint8_t>& out) {
  std::ifstream in(pak_path, std::ios::binary);
  if (!in) return false;
  std::vector<uint8_t> pak((std::istreambuf_iterator<char>(in)), {});

  std::vector<uint8_t> csf, cmf, jpg, metadata;
  if (!ReadPakFile(pak, "data/art/_avatar/p_test.csf", csf) ||
      !ReadPakFile(pak, "data/art/_avatar/dummymodel.cmf", cmf) ||
      !ReadPakFile(pak, "data/art/_avatar/avatar_body_texture.jpg", jpg) ||
      !ReadPakFile(pak, "data/art/_avatar/credits/benja.amd", metadata) || metadata.size() != 1000) {
    REXLOG_ERROR("Avatar: couldn't read the stand-in character from {}", pak_path.string());
    return false;
  }
  std::vector<Bone> bones;
  std::vector<Sub> subs;
  if (!LoadCsf(csf, bones) || !LoadCmf(cmf, subs)) {
    REXLOG_ERROR("Avatar: stand-in character data in data.pak is not as expected");
    return false;
  }
  int w = 0, h = 0, comp = 0;
  uint8_t* px = stbi_load_from_memory(jpg.data(), int(jpg.size()), &w, &h, &comp, 3);
  if (!px) {
    REXLOG_ERROR("Avatar: couldn't decode the stand-in body texture");
    return false;
  }
  Texture body{uint32_t(w), uint32_t(h), {}};
  body.argb.resize(size_t(w) * h);
  for (size_t i = 0; i < body.argb.size(); ++i)
    body.argb[i] = 0xFF000000u | uint32_t(px[3 * i]) << 16 | uint32_t(px[3 * i + 1]) << 8 | px[3 * i + 2];
  stbi_image_free(px);

  std::unordered_map<std::string, int> cal_index;
  for (int i = 0; i < int(bones.size()); ++i) cal_index[bones[i].name] = i;
  std::vector<int> joint_cal;
  std::unordered_map<int, int> cal_to_avatar;
  for (int i = 0; i < kJoints; ++i) {
    auto it = cal_index.find(kJointToCalBone[i]);
    if (it == cal_index.end()) {
      REXLOG_ERROR("Avatar: stand-in skeleton has no bone {}", kJointToCalBone[i]);
      return false;
    }
    joint_cal.push_back(it->second);
    cal_to_avatar[it->second] = i;
  }

  out.assign({'D', 'C', 'S', 'A'});
  PushLE32(out, 1);
  out.insert(out.end(), metadata.begin(), metadata.end());
  for (bool with_body : {false, true}) {
    for (int coord : {0, 1}) {
      Variant v;
      BuildVariant(bones, subs, body, coord == 0, with_body, v, joint_cal, cal_to_avatar);
      PushLE32(out, uint32_t(v.cpu.b.size()));
      PushLE32(out, uint32_t(v.gpu.b.size()));
      PushLE32(out, uint32_t(v.cpu_relocs.size()));
      PushLE32(out, uint32_t(v.gpu_relocs.size()));
      for (auto r : v.cpu_relocs) PushLE32(out, r);
      for (auto r : v.gpu_relocs) PushLE32(out, r);
      out.insert(out.end(), v.cpu.b.begin(), v.cpu.b.end());
      out.insert(out.end(), v.gpu.b.begin(), v.gpu.b.end());
    }
  }
  return true;
}

}  // namespace dcc_avatar
