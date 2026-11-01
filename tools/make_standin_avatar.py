#!/usr/bin/env python3
"""
Builds game/standin_avatar.bin: a ready-made avatar for the XAM avatar hooks
in src/avatar_standin.cpp, made from the game's own stand-in character
(data/art/_avatar/DummyModel.cmf + p_test.csf + avatar_body_texture.jpg).

The console normally assembles an avatar from the system AvatarAssetPack.
Without one, this file supplies the same guest-side structures
(X_AVATAR_ASSETS: skeleton, one body component, triangle batches, textures)
pre-laid-out in big-endian guest format, plus relocation lists so the hook only
has to copy the bytes and add the buffer base addresses.

Layouts follow gibbed's xenia avatars branch (src/xenia/avatars/guest_asset.h)
and the game's own avatar code (vertex declaration at 0x82395EC8, draw loop at
0x82392028, parameter binder at 0x823927D8).

usage: make_standin_avatar.py <data.pak> <out.bin>
"""
import io
import math
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, __file__.rsplit('/', 1)[0])
from pak_extract import entries  # noqa: E402

import lzma

AVATAR_JOINT_TO_CAL_BONE = [
    'BASE', 'BACKA', 'LF_H', 'RT_H', 'SC_BASE', 'BACKB', 'LF_K', 'LF_SC_H',
    'RT_K', 'RT_SC_H', 'SC_BACKA', 'LF_A', 'LF_C', 'LF_SC_K', 'NECK', 'RT_A',
    'RT_C', 'RT_SC_K', 'SC_BACKB', 'HEAD', 'LF_S', 'LF_T', 'RT_S', 'RT_T',
    'SC_NECK', 'LF_E', 'LF_SC_S', 'LF_SC_S_SKIN', 'RT_E', 'RT_SC_S',
    'RT_SC_S_SKIN', 'LF_E_SKIN', 'LF_SC_E', 'LF_W', 'RT_E_SKIN', 'RT_SC_E',
    'RT_W', 'LF_FINGA', 'LF_FINGB', 'LF_FINGC', 'LF_FINGD', 'LF_PROP',
    'LF_SPECIAL', 'LF_THUMB', 'RT_FINGA', 'RT_FINGB', 'RT_FINGC', 'RT_FINGD',
    'RT_PROP', 'RT_SPECIAL', 'RT_THUMB', 'LF_FINGA1', 'LF_FINGB1', 'LF_FINGC1',
    'LF_FINGD1', 'LF_THUMB1', 'RT_FINGA1', 'RT_FINGB1', 'RT_FINGC1',
    'RT_FINGD1', 'RT_THUMB1', 'LF_FINGA2', 'LF_FINGB2', 'LF_FINGC2',
    'LF_FINGD2', 'LF_THUMB2', 'RT_FINGA2', 'RT_FINGB2', 'RT_FINGC2',
    'RT_FINGD2', 'RT_THUMB2',
]  # the game's own joint-name table at 0x82A71E20 has this order

UV_COUNT = 3                      # body shader reads TEXCOORD0..2
VERTEX_SIZE = 32 + 4 * (UV_COUNT - 1)
SHADER_ID = 0                     # body/clothing shader, as real avatar parts use (4 = head, 2 = shiny)
TEX_FORMAT_ARGB8 = 0x18280086     # k_8_8_8_8, linear, 8in32
MALE_BODY_ASSET = struct.pack('>IHH8s', 2, 0, 1, bytes.fromhex('C1C8F109A19CB2E0'))
CATEGORY_BODY = 1 << 1

# Material order in DummyModel.mat.txt: mouth, eye, eye, eyebrows, eyebrows, body
SOLID_COLOURS = {  # submesh -> ARGB for the face parts
    0: 0xFF8A3B32,  # mouth
    1: 0xFF2A1E18,  # eyes
    2: 0xFF2A1E18,
    3: 0xFF4A2E1A,  # eyebrows
    4: 0xFF4A2E1A,
}


def read_pak_file(pak, name):
    for n, off, size, flags in entries(pak):
        if n.lower() == name.lower():
            blob = pak[off:off + size]
            return lzma.LZMADecompressor(lzma.FORMAT_ALONE).decompress(blob)
    raise KeyError(name)


def load_csf(d):
    n = struct.unpack_from('<I', d, 8)[0]
    p = 12
    bones = []
    for _ in range(n):
        ln = struct.unpack_from('<I', d, p)[0]; p += 4
        name = d[p:p + ln].rstrip(b'\0').decode(); p += ln
        p += 28
        lt = struct.unpack_from('<3f', d, p)
        lr = struct.unpack_from('<4f', d, p + 12); p += 28
        parent, nchild = struct.unpack_from('<ii', d, p); p += 8 + 4 * nchild
        bones.append(dict(name=name, parent=parent, lt=lt, lr=lr))
    return bones


def load_cmf(d):
    nsub = struct.unpack_from('<I', d, 12)[0]
    p = 16
    subs = []
    for _ in range(nsub):
        mat, nv, nf, nlod, nspr, nmap = struct.unpack_from('<iiiiii', d, p); p += 24
        verts = []
        for _ in range(nv):
            pos = struct.unpack_from('<3f', d, p)
            nrm = struct.unpack_from('<3f', d, p + 12); p += 32
            uvs = [struct.unpack_from('<2f', d, p + 8 * k) for k in range(nmap)]; p += 8 * nmap
            ninf = struct.unpack_from('<i', d, p)[0]; p += 4
            infl = [struct.unpack_from('<if', d, p + 8 * k) for k in range(ninf)]; p += 8 * ninf
            if nspr:
                p += 4
            verts.append(dict(pos=pos, nrm=nrm, uvs=uvs, infl=infl))
        p += nspr * 24
        faces = [struct.unpack_from('<3i', d, p + 12 * k) for k in range(nf)]; p += 12 * nf
        subs.append(dict(verts=verts, faces=faces))
    assert p == len(d)
    return subs


def qrot_inv(q, v):
    x, y, z, w = q
    x, y, z = -x, -y, -z
    vx, vy, vz = v
    tx, ty, tz = 2 * (y * vz - z * vy), 2 * (z * vx - x * vz), 2 * (x * vy - y * vx)
    return (vx + w * tx + (y * tz - z * ty),
            vy + w * ty + (z * tx - x * tz),
            vz + w * tz + (x * ty - y * tx))


def to_avatar(v, flip_z):
    # Cal3D is Z-up; avatar space is Y-up: (x, y, z) = (x, z_cal, -y_cal).
    x, y, z = v[0], v[2], -v[1]
    return (x, y, -z if flip_z else z)


def f16(v):
    return struct.unpack('>H', struct.pack('>e', v))[0]


def pack_hend3n(n):
    x, y, z = n
    l = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / l, y / l, z / l
    xi = int(round(max(-1, min(1, x)) * 1023)) & 0x7FF
    yi = int(round(max(-1, min(1, y)) * 1023)) & 0x7FF
    zi = int(round(max(-1, min(1, z)) * 511)) & 0x3FF
    return xi | (yi << 11) | (zi << 22)


class Buf:
    def __init__(self, align):
        self.b = bytearray()
        self.align = align

    def claim(self, size, align=None):
        a = align or self.align
        off = (len(self.b) + a - 1) & ~(a - 1)
        self.b.extend(b'\0' * (off + size - len(self.b)))
        return off

    def put(self, off, fmt, *vals):
        struct.pack_into('>' + fmt, self.b, off, *vals)


def texture_bytes(rgba_rows, width, height):
    pitch = (width * 4 + 511) & ~511
    out = bytearray(pitch * height)
    for y in range(height):
        row = rgba_rows[y]
        for x in range(width):
            a, r, g, b = row[x]
            struct.pack_into('>I', out, y * pitch + x * 4, (a << 24) | (r << 16) | (g << 8) | b)
    return bytes(out), pitch


def build_variant(bones, subs, body_tex, flip_z, with_body):
    cal_index = {b['name']: i for i, b in enumerate(bones)}
    cal_to_avatar = {cal_index[n]: i for i, n in enumerate(AVATAR_JOINT_TO_CAL_BONE)}

    world = [qrot_inv(b['lr'], tuple(-c for c in b['lt'])) for b in bones]
    joint_pos = [to_avatar(world[cal_index[n]], flip_z) for n in AVATAR_JOINT_TO_CAL_BONE]
    joint_parent = []
    for n in AVATAR_JOINT_TO_CAL_BONE:
        cp = bones[cal_index[n]]['parent']
        joint_parent.append(255 if cp < 0 else cal_to_avatar[cp])

    cpu = Buf(16)
    gpu = Buf(4096)
    cpu_relocs = []   # offsets of u32 fields that get + cpu_base
    gpu_relocs = []   # offsets (in cpu) of u32 fields that get + gpu_base

    def cptr(field_off, target_off):
        cpu.put(field_off, 'I', target_off); cpu_relocs.append(field_off)

    def gptr(field_off, target_off):
        cpu.put(field_off, 'I', target_off); gpu_relocs.append(field_off)

    assets = cpu.claim(20)
    skel = cpu.claim(8)
    cptr(assets + 0, skel)

    # ---- skeleton (X_AVATAR_SKELETON_JOINT, 96 bytes each) ----
    nj = len(joint_pos)
    joints = cpu.claim(96 * nj)
    cpu.put(skel, 'I', nj)
    cptr(skel + 4, joints)
    first_child = [255] * nj
    next_sib = [255] * nj
    for parent in range(nj):
        kids = [c for c in range(nj) if joint_parent[c] == parent]
        if kids:
            first_child[parent] = kids[0]
            for a, b in zip(kids, kids[1:]):
                next_sib[a] = b
    for i in range(nj):
        o = joints + 96 * i
        px, py, pz = joint_pos[i]
        if joint_parent[i] == 255:
            lx, ly, lz = px, py, pz
        else:
            qx, qy, qz = joint_pos[joint_parent[i]]
            lx, ly, lz = px - qx, py - qy, pz - qz
        cpu.put(o, 'IIII', joint_parent[i], first_child[i], next_sib[i], 0)
        cpu.put(o + 16, '8f', px, py, pz, 1.0, 0.0, 0.0, 0.0, 1.0)            # bindpose
        cpu.put(o + 48, '12f', lx, ly, lz, 1.0, 0.0, 0.0, 0.0, 1.0,          # pose
                1.0, 1.0, 1.0, 1.0)

    if not with_body:
        cpu.put(assets + 4, 'III', 0, 0, 0)
        return cpu, gpu, cpu_relocs, gpu_relocs

    # ---- one body component ----
    info = cpu.claim(32)
    model = cpu.claim(52)
    cpu.put(assets + 8, 'I', 1)
    cptr(assets + 12, info)
    cptr(assets + 16, model)
    cpu.b[info:info + 16] = MALE_BODY_ASSET
    cpu.put(info + 16, 'H', CATEGORY_BODY)

    # textures: 0 body colour, 1 neutral (transparent), 2..6 face-part solids
    tex_specs = []
    w, h = body_tex.size
    px_ = body_tex.load()
    tex_specs.append((w, h, [[(255,) + px_[x, y][:3] for x in range(w)] for y in range(h)]))
    tex_specs.append((128, 32, [[(0, 0, 0, 0)] * 128 for _ in range(32)]))
    solid_index = {}
    for sub, argb in SOLID_COLOURS.items():
        if argb not in solid_index:
            solid_index[argb] = len(tex_specs)
            c = ((argb >> 24) & 255, (argb >> 16) & 255, (argb >> 8) & 255, argb & 255)
            tex_specs.append((128, 32, [[c] * 128 for _ in range(32)]))

    batch_count = len(subs)
    model_cpu_start = cpu.claim(512 * batch_count + 44 * len(tex_specs), 16)
    batches = model_cpu_start
    textures = batches + 512 * batch_count

    # vertex + index buffers (one each for the model)
    vb_parts, ib_parts = [], []
    for si, sub in enumerate(subs):
        vb = bytearray()
        for v in sub['verts']:
            pos = to_avatar(v['pos'], flip_z)
            nrm = to_avatar(v['nrm'], flip_z)
            infl = sorted(v['infl'], key=lambda t: -t[1])[:4]
            tot = sum(wt for _, wt in infl) or 1.0
            idx = [cal_to_avatar.get(b, 0) for b, _ in infl]
            wts = [int(round(wt / tot * 255)) for _, wt in infl]
            if wts:
                wts[0] += 255 - sum(wts)
            while len(idx) < 4:
                idx.append(idx[0] if idx else 0)
                wts.append(0)
            # component X is the low byte of the big-endian dword
            bw = wts[0] | (wts[1] << 8) | (wts[2] << 16) | (wts[3] << 24)
            bi = idx[0] | (idx[1] << 8) | (idx[2] << 16) | (idx[3] << 24)
            if os.environ.get('STANDIN_RIGID'):   # debugging: bind everything to joint 0
                bw, bi = 0xFF, 0
            u, vv = v['uvs'][0] if v['uvs'] else (0.0, 0.0)
            rec = struct.pack('>3fIIII', *pos, pack_hend3n(nrm), bw, bi, 0xFFFFFFFF)
            for _ in range(UV_COUNT):
                rec += struct.pack('>HH', f16(u), f16(vv))
            assert len(rec) == VERTEX_SIZE
            vb += rec
        ib = bytearray()
        for a, b, c in sub['faces']:
            if flip_z:   # mirroring flips winding
                b, c = c, b
            ib += struct.pack('>HHH', a, b, c)
        vb_parts.append(vb)
        ib_parts.append(ib)

    vb_off = gpu.claim(sum(len(x) for x in vb_parts), 4096)
    vertex_offsets = []
    o = vb_off
    for vb in vb_parts:
        gpu.b[o:o + len(vb)] = vb
        vertex_offsets.append(o)
        o += len(vb)
    vb_size = o - vb_off
    ib_off = gpu.claim(sum(len(x) for x in ib_parts), 4096)
    index_offsets = []
    o = ib_off
    for ib in ib_parts:
        gpu.b[o:o + len(ib)] = ib
        index_offsets.append(o)
        o += len(ib)
    ib_size = o - ib_off

    tex_start = None
    tex_data_offsets = []
    for (tw, th, rows) in tex_specs:
        data, pitch = texture_bytes(rows, tw, th)
        off = gpu.claim(len(data), 4096)
        tex_start = off if tex_start is None else tex_start
        gpu.b[off:off + len(data)] = data
        tex_data_offsets.append((off, len(data)))
    tex_size = len(gpu.b) - tex_start

    for ti, ((tw, th, _), (doff, dsize)) in enumerate(zip(tex_specs, tex_data_offsets)):
        t = textures + 44 * ti
        cpu.put(t, '9I', TEX_FORMAT_ARGB8, tw, th, dsize, 0, dsize, 0, 1, 1)
        gptr(t + 36, doff)
        cpu.put(t + 40, 'I', 0)

    for si, sub in enumerate(subs):
        bo = batches + 512 * si
        cpu.put(bo, 'I', SHADER_ID)
        colour_tex = 0 if si not in SOLID_COLOURS else solid_index[SOLID_COLOURS[si]]
        params = [
            (colour_tex, 0),   # s0: colour
            (1, 1),            # s1: intensity (alpha 0 = no tint)
            (1, 2),            # s2: decal (alpha 0 = none)
        ]
        for pi, (tex, sampler) in enumerate(params):
            po = bo + 4 + 24 * pi
            cpu.put(po, 'IIHHI', 1, pi + 1, tex, sampler, 0)   # usages 1-3 as in real parts
        # Pixel-shader constants (type 3). Without them the shader's tint and
        # rim-light registers keep stale values and the whole model renders as
        # one flat colour. Usages follow gibbed's overrides: 22-24 tint colours
        # (CustomColor1-3), 25 transparency, 26 reflectivity, 27 rim light.
        consts = [
            (22, (1.0, 1.0, 1.0, 1.0)),
            (23, (1.0, 1.0, 1.0, 1.0)),
            (24, (1.0, 1.0, 1.0, 1.0)),
            (25, (0.0, 0.0, 0.0, 0.0)),
            (26, (0.0, 0.0, 0.0, 0.0)),
            (27, (0.54901963, 0.5019608, 0.40392157, 1.6)),
        ]
        for ci, (usage, vals) in enumerate(consts):
            po = bo + 4 + 24 * (len(params) + ci)
            cpu.put(po, 'II4f', 3, usage, *vals)
        tail = bo + 4 + 24 * 20
        cpu.put(tail, '5I', len(sub['faces']), len(sub['verts']), UV_COUNT, VERTEX_SIZE, 2)
        gptr(tail + 20, vertex_offsets[si])
        gptr(tail + 24, index_offsets[si])

    model_cpu_size = textures + 44 * len(tex_specs) - model_cpu_start
    cpu.put(model, '7I', model_cpu_size, len(gpu.b), tex_size, vb_size, ib_size,
            batch_count, len(tex_specs))
    cptr(model + 28, model_cpu_start)
    gptr(model + 32, 0)
    gptr(model + 36, vb_off)
    gptr(model + 40, ib_off)
    cptr(model + 44, batches)
    cptr(model + 48, textures)
    return cpu, gpu, cpu_relocs, gpu_relocs


def main(pak_path, out_path):
    pak = open(pak_path, 'rb').read()
    bones = load_csf(read_pak_file(pak, 'data/art/_avatar/p_test.csf'))
    subs = load_cmf(read_pak_file(pak, 'data/art/_avatar/dummymodel.cmf'))
    body_tex = Image.open(io.BytesIO(read_pak_file(pak, 'data/art/_avatar/avatar_body_texture.jpg'))).convert('RGB')
    metadata = read_pak_file(pak, 'data/art/_avatar/credits/benja.amd')
    assert len(metadata) == 1000

    out = bytearray(b'DCSA' + struct.pack('<I', 1))
    out += metadata
    # variants: (coordinate_system, with_body) in order (0,F) (1,F) (0,T) (1,T)
    for with_body in (False, True):
        for coord in (0, 1):
            cpu, gpu, cr, gr = build_variant(bones, subs, body_tex, flip_z=(coord == 0), with_body=with_body)
            out += struct.pack('<IIII', len(cpu.b), len(gpu.b), len(cr), len(gr))
            out += struct.pack('<%dI' % len(cr), *cr) + struct.pack('<%dI' % len(gr), *gr)
            out += cpu.b + gpu.b
            print(f'coord {coord} body {with_body}: cpu {len(cpu.b)} gpu {len(gpu.b)} relocs {len(cr)}+{len(gr)}')
    open(out_path, 'wb').write(out)
    print('wrote', out_path, len(out), 'bytes')


if __name__ == '__main__':
    main(*sys.argv[1:])
