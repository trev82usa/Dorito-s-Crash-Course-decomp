// The handful of DirectXMath functions gibbed's skeleton code uses, written out
// so the avatar code builds on any platform (DirectXMath conventions: row
// vectors, XMMatrixMultiply(A, B) = A * B).
#pragma once

#include <cmath>

namespace DirectX {

struct XMVECTOR {
  float v[4];
};
struct XMMATRIX {
  XMVECTOR r[4];
};

inline XMVECTOR XMVectorSet(float x, float y, float z, float w) {
  return {{x, y, z, w}};
}
inline float XMVectorGetX(const XMVECTOR& a) { return a.v[0]; }
inline float XMVectorGetY(const XMVECTOR& a) { return a.v[1]; }
inline float XMVectorGetZ(const XMVECTOR& a) { return a.v[2]; }
inline float XMVectorGetW(const XMVECTOR& a) { return a.v[3]; }

inline XMMATRIX XMMatrixRotationQuaternion(const XMVECTOR& q) {
  float x = q.v[0], y = q.v[1], z = q.v[2], w = q.v[3];
  float xx = x * x, yy = y * y, zz = z * z;
  float xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
  XMMATRIX m;
  m.r[0] = {{1 - 2 * (yy + zz), 2 * (xy + wz), 2 * (xz - wy), 0}};
  m.r[1] = {{2 * (xy - wz), 1 - 2 * (xx + zz), 2 * (yz + wx), 0}};
  m.r[2] = {{2 * (xz + wy), 2 * (yz - wx), 1 - 2 * (xx + yy), 0}};
  m.r[3] = {{0, 0, 0, 1}};
  return m;
}

inline XMMATRIX XMMatrixMultiply(const XMMATRIX& a, const XMMATRIX& b) {
  XMMATRIX m;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) {
      float s = 0;
      for (int k = 0; k < 4; ++k) s += a.r[i].v[k] * b.r[k].v[j];
      m.r[i].v[j] = s;
    }
  return m;
}

inline XMMATRIX XMMatrixInverse(XMVECTOR* det_out, const XMMATRIX& in) {
  float m[16], inv[16];
  for (int i = 0; i < 16; ++i) m[i] = in.r[i / 4].v[i % 4];
  inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
           m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
  inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
           m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
  inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
           m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
  inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
            m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
  inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
           m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
  inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
           m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
  inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
           m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
  inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
            m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
  inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
           m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
  inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
           m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
  inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
            m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
  inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
            m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
  inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
           m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
  inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
           m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
  inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
            m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
  inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
            m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
  float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
  if (det_out) *det_out = XMVectorSet(det, det, det, det);
  XMMATRIX out;
  float r = det != 0 ? 1.0f / det : 0.0f;
  for (int i = 0; i < 16; ++i) out.r[i / 4].v[i % 4] = inv[i] * r;
  return out;
}

inline XMVECTOR XMQuaternionRotationMatrix(const XMMATRIX& m) {
  float m00 = m.r[0].v[0], m01 = m.r[0].v[1], m02 = m.r[0].v[2];
  float m10 = m.r[1].v[0], m11 = m.r[1].v[1], m12 = m.r[1].v[2];
  float m20 = m.r[2].v[0], m21 = m.r[2].v[1], m22 = m.r[2].v[2];
  float x, y, z, w, tr = m00 + m11 + m22;
  // Row-vector matrices: the rotation is the transpose of the column form.
  if (tr > 0) {
    float s = std::sqrt(tr + 1.0f) * 2;
    w = 0.25f * s;
    x = (m12 - m21) / s;
    y = (m20 - m02) / s;
    z = (m01 - m10) / s;
  } else if (m00 > m11 && m00 > m22) {
    float s = std::sqrt(1.0f + m00 - m11 - m22) * 2;
    w = (m12 - m21) / s;
    x = 0.25f * s;
    y = (m01 + m10) / s;
    z = (m20 + m02) / s;
  } else if (m11 > m22) {
    float s = std::sqrt(1.0f + m11 - m00 - m22) * 2;
    w = (m20 - m02) / s;
    x = (m01 + m10) / s;
    y = 0.25f * s;
    z = (m12 + m21) / s;
  } else {
    float s = std::sqrt(1.0f + m22 - m00 - m11) * 2;
    w = (m01 - m10) / s;
    x = (m20 + m02) / s;
    y = (m12 + m21) / s;
    z = 0.25f * s;
  }
  return XMVectorSet(x, y, z, w);
}

}  // namespace DirectX
