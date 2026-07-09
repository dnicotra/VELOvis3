#include "GpuBatch.hpp"

#include "raymath.h"
#include "rlgl.h"
#include <cmath>
#include <cstdlib>
#include <utility>

// A box as 12 triangles (36 vertices) over its 8 corners. Winding is not
// relied upon — the scene draws with backface culling disabled — so this same
// index table serves both the axis-aligned hit cubes and the segment prisms
// (whose local "z" axis runs along the segment).
static const int kBoxTris[36] = {
    4, 5, 6,  4, 6, 7,   // +z
    1, 0, 3,  1, 3, 2,   // -z
    5, 1, 2,  5, 2, 6,   // +x
    0, 4, 7,  0, 7, 3,   // -x
    3, 2, 6,  3, 6, 7,   // +y
    0, 1, 5,  0, 5, 4,   // -y
};

GpuBatch::~GpuBatch() { Clear(); }

GpuBatch::GpuBatch(GpuBatch&& o) noexcept { *this = std::move(o); }

GpuBatch& GpuBatch::operator=(GpuBatch&& o) noexcept {
    if (this != &o) {
        Clear();
        mesh_ = o.mesh_;   mat_ = o.mat_;
        uploaded_ = o.uploaded_; hasMat_ = o.hasMat_;
        o.mesh_ = Mesh{};  o.uploaded_ = false; o.hasMat_ = false;
    }
    return *this;
}

void GpuBatch::Clear() {
    if (uploaded_) {
        UnloadMesh(mesh_);   // frees VAO/VBOs and the malloc'd CPU arrays
        mesh_ = Mesh{};
        uploaded_ = false;
    }
    // mat_ references the default shader/texture — nothing of ours to unload.
}

void GpuBatch::Upload(int vertexCount, float* verts, unsigned char* colors) {
    Clear();
    mesh_               = Mesh{};
    mesh_.vertexCount   = vertexCount;
    mesh_.triangleCount = vertexCount / 3;   // non-indexed: sidesteps the 16-bit index cap
    mesh_.vertices      = verts;
    mesh_.colors        = colors;
    UploadMesh(&mesh_, false);
    if (!hasMat_) { mat_ = LoadMaterialDefault(); hasMat_ = true; }
    uploaded_ = true;
}

void GpuBatch::BuildCubes(const std::vector<Vector3>& centers,
                          const std::vector<Color>&   colors,
                          float size) {
    const int n = (int)centers.size();
    if (n == 0) { Clear(); return; }

    const int      vcount = n * 36;
    float*         verts  = (float*)std::malloc(sizeof(float) * 3 * vcount);
    unsigned char* cols   = (unsigned char*)std::malloc((size_t)4 * vcount);

    const float h = size * 0.5f;
    const Vector3 corner[8] = {
        {-h, -h, -h}, { h, -h, -h}, { h, h, -h}, {-h, h, -h},
        {-h, -h,  h}, { h, -h,  h}, { h, h,  h}, {-h, h,  h},
    };

    int vi = 0;
    for (int i = 0; i < n; ++i) {
        const Vector3 c = centers[i];
        const Color   k = (i < (int)colors.size()) ? colors[i] : WHITE;
        for (int t = 0; t < 36; ++t, ++vi) {
            const Vector3 p = corner[kBoxTris[t]];
            verts[vi * 3 + 0] = c.x + p.x;
            verts[vi * 3 + 1] = c.y + p.y;
            verts[vi * 3 + 2] = c.z + p.z;
            cols[vi * 4 + 0] = k.r; cols[vi * 4 + 1] = k.g;
            cols[vi * 4 + 2] = k.b; cols[vi * 4 + 3] = k.a;
        }
    }
    Upload(vcount, verts, cols);
}

void GpuBatch::BuildSegments(const std::vector<Vector3>& A,
                             const std::vector<Vector3>& B,
                             const std::vector<Color>&   colors,
                             float width) {
    const int n = (int)A.size();
    if (n == 0) { Clear(); return; }

    const int      vcount = n * 36;
    float*         verts  = (float*)std::malloc(sizeof(float) * 3 * vcount);
    unsigned char* cols   = (unsigned char*)std::malloc((size_t)4 * vcount);

    const float hw = width * 0.5f;
    int vi = 0;
    for (int i = 0; i < n; ++i) {
        const Vector3 a = A[i], b = B[i];

        // Orthonormal frame: d along the segment, (u,v) span its cross-section.
        Vector3 d   = Vector3Subtract(b, a);
        float   len = Vector3Length(d);
        d = (len > 1e-6f) ? Vector3Scale(d, 1.0f / len) : Vector3{0, 0, 1};
        const Vector3 ref = (fabsf(d.y) < 0.99f) ? Vector3{0, 1, 0} : Vector3{1, 0, 0};
        const Vector3 u   = Vector3Normalize(Vector3CrossProduct(ref, d));
        const Vector3 v   = Vector3CrossProduct(d, u);
        const Vector3 uu  = Vector3Scale(u, hw);
        const Vector3 vv  = Vector3Scale(v, hw);

        // Corners laid out to match the cube's (±u, ±v) pattern, a-end then b-end.
        const Vector3 crn[8] = {
            Vector3Subtract(Vector3Subtract(a, uu), vv),
            Vector3Subtract(Vector3Add(a, uu), vv),
            Vector3Add(Vector3Add(a, uu), vv),
            Vector3Add(Vector3Subtract(a, uu), vv),
            Vector3Subtract(Vector3Subtract(b, uu), vv),
            Vector3Subtract(Vector3Add(b, uu), vv),
            Vector3Add(Vector3Add(b, uu), vv),
            Vector3Add(Vector3Subtract(b, uu), vv),
        };

        const Color k = (i < (int)colors.size()) ? colors[i] : WHITE;
        for (int t = 0; t < 36; ++t, ++vi) {
            const Vector3 p = crn[kBoxTris[t]];
            verts[vi * 3 + 0] = p.x;
            verts[vi * 3 + 1] = p.y;
            verts[vi * 3 + 2] = p.z;
            cols[vi * 4 + 0] = k.r; cols[vi * 4 + 1] = k.g;
            cols[vi * 4 + 2] = k.b; cols[vi * 4 + 3] = k.a;
        }
    }
    Upload(vcount, verts, cols);
}

void GpuBatch::BuildTriangles(const std::vector<Vector3>& v,
                              const std::vector<Color>&   c) {
    const int vcount = (int)v.size();
    if (vcount < 3) { Clear(); return; }

    float*         verts = (float*)std::malloc(sizeof(float) * 3 * vcount);
    unsigned char* cols  = (unsigned char*)std::malloc((size_t)4 * vcount);
    for (int i = 0; i < vcount; ++i) {
        verts[i * 3 + 0] = v[i].x; verts[i * 3 + 1] = v[i].y; verts[i * 3 + 2] = v[i].z;
        const Color k = (i < (int)c.size()) ? c[i] : WHITE;
        cols[i * 4 + 0] = k.r; cols[i * 4 + 1] = k.g;
        cols[i * 4 + 2] = k.b; cols[i * 4 + 3] = k.a;
    }
    Upload(vcount, verts, cols);
}

void GpuBatch::UpdateVertices(const std::vector<Vector3>& verts,
                             const std::vector<Color>&   colors) {
    if (!uploaded_ || (int)verts.size() != mesh_.vertexCount) return;
    const int vcount = mesh_.vertexCount;
    for (int i = 0; i < vcount; ++i) {
        mesh_.vertices[i * 3 + 0] = verts[i].x;
        mesh_.vertices[i * 3 + 1] = verts[i].y;
        mesh_.vertices[i * 3 + 2] = verts[i].z;
        const Color k = (i < (int)colors.size()) ? colors[i] : WHITE;
        mesh_.colors[i * 4 + 0] = k.r; mesh_.colors[i * 4 + 1] = k.g;
        mesh_.colors[i * 4 + 2] = k.b; mesh_.colors[i * 4 + 3] = k.a;
    }
    rlUpdateVertexBuffer(mesh_.vboId[0], mesh_.vertices, vcount * 3 * (int)sizeof(float), 0);
    rlUpdateVertexBuffer(mesh_.vboId[3], mesh_.colors,   vcount * 4 * (int)sizeof(unsigned char), 0);
}

void GpuBatch::Draw(Color tint) const {
    if (!uploaded_) return;
    Material m = mat_;                              // shallow copy; tint is per-draw
    m.maps[MATERIAL_MAP_DIFFUSE].color = tint;      // default shader: tex * colDiffuse * vertexColor
    DrawMesh(mesh_, m, MatrixIdentity());
}
