#pragma once
#include "raylib.h"
#include "EventReader.hpp"
#include "GpuBatch.hpp"
#include <string>
#include <vector>

// How an element's color is chosen. The mode is resolved at (re)build time and
// baked into per-vertex colors; global alpha rides on the draw tint instead.
struct ColorSpec {
    int   mode = 0;                    // index into the layer's mode list
    Color base = {80, 160, 255, 255};  // used by the "Uniform" mode
};

// Per-track truth metadata driving property-based coloring. Optional: a generic
// track set with no such data leaves this empty and only offers the Uniform
// color mode — the property modes fall back to the base color.
struct TrackMeta {
    int   pid = 0;
    int   chargeSign = 0;
    float p = 0, pt = 0, eta = 0, phi = 0;
    bool  isLong = false, fromBeauty = false;
};

// A track is an ordered list of indices into a shared hit-position array; each
// consecutive pair is one drawn segment. A bare "set of segments" is therefore
// just a set of two-index tracks — nothing here is specific to MC particles.
struct Track {
    std::vector<int> hitIndices;
};

// A drawable set of tracks sharing the scene's hit positions.
struct TrackLayer {
    std::string            name;
    std::vector<Track>     tracks;
    std::vector<TrackMeta> meta;      // parallel to tracks, or empty
    bool      visible = true;
    float     width   = 1.5f;
    float     alpha   = 1.0f;
    ColorSpec color;
    GpuBatch  batch;
    bool      dirty        = true;
    int       segmentCount = 0;
};

// A drawable point cloud (the event's hits).
struct HitLayer {
    std::string name = "Hits";
    bool      visible = true;
    float     size    = 2.0f;
    float     alpha   = 1.0f;
    ColorSpec color   = {0, {255, 255, 255, 255}};  // Uniform white by default
    GpuBatch  batch;
    bool      dirty = true;
};

// The VELO detector overlay: four thin extruded sensor tiles per module, arranged
// as the Upgrade double-L footprint. A-side modules are positive x / odd indices;
// C-side modules are negative x / even indices. Module z-positions come from the
// event hits. Transparent and hideable.
struct DetectorLayer {
    std::string name = "VELO detector";
    bool   visible   = true;
    float  alpha     = 0.18f;                  // low: it's a translucent overlay
    Color  colorA    = {70, 140, 230, 255};    // A-side (positive x, odd modules)
    Color  colorC    = {235, 130, 55, 255};    // C-side (negative x, even modules)
    float  thickness = 4.0f;                    // box z-thickness (mm)
    float  edgeWidth = 0.7f;                     // outline thickness (mm)
    GpuBatch batch;       // shaded translucent box faces
    GpuBatch edgeBatch;   // bright wireframe outlines (the main 3D cue)
    bool   dirty       = true;
    int    moduleCount = 0;

    // CPU-side copy of the baked face triangles, kept so they can be re-sorted
    // back-to-front for correct alpha compositing between overlapping modules as
    // the camera moves. faceVerts holds 3 verts per triangle in build order.
    std::vector<Vector3> faceVerts;
    std::vector<Color>   faceCols;       // parallel to faceVerts
    std::vector<Vector3> faceCentroid;   // one per triangle (faceVerts.size()/3)
    std::vector<int>     triOrder;       // scratch: triangle indices, sorted far→near
    std::vector<Vector3> sortVerts;      // scratch: gathered sorted verts
    std::vector<Color>   sortCols;       // scratch: gathered sorted colors
    Vector3 lastSortCam{0, 0, 0};        // camera position at the last sort
    bool    sortValid = false;           // reset on rebuild; drives the idle skip
};

// Owns the hit positions plus one hit layer and any number of track layers,
// builds them from a VeloEvent, and renders/inspects them. General by design:
// add reconstructed tracks, seeds, etc. as extra TrackLayers.
class EventScene {
public:
    void SetEvent(const velo::VeloEvent& ev);
    void Clear();

    void Update();          // rebuild any dirty layer batches (needs a GL context)
    void Draw(Vector3 camPos); // call inside BeginMode3D; camPos drives depth sorting
    void DrawInspectorUI(); // ImGui controls for every layer

    BoundingBox Bounds() const { return bounds_; }
    bool        Empty()  const { return positions_.empty(); }

private:
    void RebuildHits(HitLayer& l);
    void RebuildTracks(TrackLayer& l);
    void RebuildDetector(DetectorLayer& l);
    void SortDetector(Vector3 camPos);  // re-sort detector faces back-to-front

    std::vector<Vector3> positions_;    // hit positions (world space)
    std::vector<int>     module_;       // per-hit module index (from prefix sums)
    std::vector<int>     ownerPid_;     // per-hit first referencing MC pid (0 if none)
    std::vector<int>     ownerCharge_;  // per-hit first referencing MC charge sign
    std::vector<float>   moduleZ0_;     // lower sensor-face z per module (1e30 if empty)
    std::vector<float>   moduleZ1_;     // upper sensor-face z per module

    HitLayer                hitLayer_;
    std::vector<TrackLayer> trackLayers_;
    DetectorLayer           detector_;
    BoundingBox             bounds_{};
};
