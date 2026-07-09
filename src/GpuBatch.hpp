#pragma once
#include "raylib.h"
#include <vector>

// A GPU-resident, vertex-colored triangle mesh drawn in a single DrawMesh call.
// Two builders bake the event-display primitives we need into one batch each:
//   - BuildCubes:    one small axis-aligned cube per point   (hits)
//   - BuildSegments: one thin oriented box per line segment  (track polylines)
//
// Rebuild (re-bake + re-upload) only when geometry or per-element color changes.
// Global alpha/brightness is applied per frame via the Draw() tint, which the
// default shader multiplies with the baked vertex colors — so dragging an alpha
// slider costs nothing (no rebuild). Plain triangles + vertex colors keep it
// portable across desktop GL 3.3 and web GLES2/WebGL.
class GpuBatch {
public:
    GpuBatch() = default;
    ~GpuBatch();

    GpuBatch(const GpuBatch&)            = delete;
    GpuBatch& operator=(const GpuBatch&) = delete;
    GpuBatch(GpuBatch&& o) noexcept;
    GpuBatch& operator=(GpuBatch&& o) noexcept;

    void BuildCubes(const std::vector<Vector3>& centers,
                    const std::vector<Color>&   colors,
                    float size);

    void BuildSegments(const std::vector<Vector3>& a,
                       const std::vector<Vector3>& b,
                       const std::vector<Color>&   colors,
                       float width);

    // Raw triangle soup: verts.size() must be a multiple of 3, one color per
    // vertex. Lets callers bake arbitrary geometry (e.g. extruded detector
    // footprints) into a single batched draw.
    void BuildTriangles(const std::vector<Vector3>& verts,
                        const std::vector<Color>&   colors);

    void Draw(Color tint) const;   // call inside BeginMode3D
    void Clear();                  // free GPU + CPU mesh
    bool Empty()       const { return !uploaded_; }
    int  VertexCount() const { return uploaded_ ? mesh_.vertexCount : 0; }

private:
    void Upload(int vertexCount, float* verts, unsigned char* colors);

    Mesh     mesh_{};
    Material mat_{};
    bool     uploaded_ = false;
    bool     hasMat_   = false;
};
