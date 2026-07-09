#include "EventScene.hpp"

#include "raymath.h"
#include "rlgl.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <unordered_map>

// ─── Color coding ──────────────────────────────────────────────────────────────
// The two building blocks — a categorical palette and a continuous colormap —
// plus per-object resolvers. To add a color mode: add a name to the mode list,
// extend the switch in the matching resolver, and (if continuous) report its
// scalar in the *Scalar helper. Nothing else needs to change.

enum HitColorMode {
    HCM_Uniform = 0, HCM_DepthZ, HCM_Module, HCM_TrackPid, HCM_TrackAbsPid, HCM_TrackCharge, HCM_Count
};
enum TrackColorMode {
    TCM_Uniform = 0, TCM_Pid, TCM_AbsPid, TCM_Charge, TCM_Pt, TCM_P, TCM_Eta, TCM_IsLong, TCM_FromB, TCM_Count
};

static const char* kHitModes[]   = {"Uniform", "Depth (z)", "Module",
                                     "Track PID", "Track |PID|", "Track charge"};
static const char* kTrackModes[] = {"Uniform", "PID", "|PID|", "Charge sign",
                                     "pT", "p", "eta", "isLong", "from B decay"};
static_assert(sizeof(kHitModes)   / sizeof(*kHitModes)   == HCM_Count, "hit mode names/enum out of sync");
static_assert(sizeof(kTrackModes) / sizeof(*kTrackModes) == TCM_Count, "track mode names/enum out of sync");

static Color Palette(int i) {
    static const Color P[] = {
        {228, 26,  28,  255}, {55,  126, 184, 255}, {77,  175, 74,  255},
        {152, 78,  163, 255}, {255, 127, 0,   255}, {255, 215, 0,   255},
        {166, 86,  40,  255}, {247, 129, 191, 255}, {0,   206, 209, 255},
        {153, 153, 153, 255}, {31,  120, 180, 255}, {178, 223, 138, 255},
        {251, 154, 153, 255}, {202, 178, 214, 255}, {253, 191, 111, 255},
        {106, 61,  154, 255},
    };
    const int n = (int)(sizeof(P) / sizeof(P[0]));
    return P[((i % n) + n) % n];
}

static Color ChargeColor(int sign) {
    if (sign > 0) return {228, 26, 28, 255};   // +
    if (sign < 0) return {55, 126, 184, 255};  // -
    return {153, 153, 153, 255};               // neutral
}

static Color Lerp(Color a, Color b, float t) {
    return {
        (unsigned char)(a.r + (b.r - a.r) * t),
        (unsigned char)(a.g + (b.g - a.g) * t),
        (unsigned char)(a.b + (b.b - a.b) * t),
        255,
    };
}

// Perceptual-ish sweep navy -> teal -> green -> amber -> red.
static Color Colormap(float t) {
    t = (t < 0) ? 0 : (t > 1) ? 1 : t;
    static const Color stops[] = {
        {40, 30, 120, 255}, {20, 140, 200, 255}, {40, 190, 120, 255},
        {230, 210, 60, 255}, {220, 60, 40, 255},
    };
    const int ns = (int)(sizeof(stops) / sizeof(stops[0]));
    float x = t * (ns - 1);
    int   i = (int)x;
    if (i >= ns - 1) i = ns - 2;
    return Lerp(stops[i], stops[i + 1], x - i);
}

// Signed-PID color: the species hue comes from |PID| (so a particle and its
// antiparticle share a color family), shaded by charge — positive lightened,
// negative darkened — so the sign stays distinguishable.
static Color PidColor(int pid, int chargeSign) {
    const Color base = Palette(pid < 0 ? -pid : pid);
    if (chargeSign > 0) return Lerp(base, Color{255, 255, 255, 255}, 0.40f);
    if (chargeSign < 0) return Lerp(base, Color{15, 15, 20, 255}, 0.45f);
    return base;
}

static float TrackScalar(int mode, const TrackMeta& m) {
    switch (mode) {
        case TCM_Pt:  return m.pt;
        case TCM_P:   return m.p;
        case TCM_Eta: return m.eta;
        default: return 0.0f;
    }
}

static Color TrackColor(const ColorSpec& cs, const TrackMeta* m, float norm) {
    if (!m) return cs.base;
    switch (cs.mode) {
        case TCM_Uniform: return cs.base;
        case TCM_Pid:     return PidColor(m->pid, m->chargeSign);
        case TCM_AbsPid:  return Palette(m->pid < 0 ? -m->pid : m->pid);
        case TCM_Charge:  return ChargeColor(m->chargeSign);
        case TCM_Pt: case TCM_P: case TCM_Eta: return Colormap(norm);
        case TCM_IsLong:  return m->isLong    ? Palette(2) : Palette(9);
        case TCM_FromB:   return m->fromBeauty ? Palette(0) : Palette(9);
        default: return cs.base;
    }
}

// C-side module footprint in the module x-y plane (mm). The Upgrade module has
// four 43.47 x 14.98 mm sensor tiles: two on each face of an L-shaped substrate.
// The two faces form complementary Ls in projection, leaving the central beam
// aperture clear. A-side modules are this layout rotated 180 deg about the beam.
static const Vector2 kModCenterC    = {-9.56f, -13.32f};
static const Vector2 kModLongAxis   = {-0.70711f, 0.70711f};
static const Vector2 kModShortAxis  = { 0.70711f, 0.70711f};
static const float   kSensorHalfLen = 21.735f;   // 43.470 mm / 2
static const float   kSensorHalfWid = 7.490f;    // 14.980 mm / 2

struct SensorFootprint {
    Vector2 localCenter; // (u, v) in the kModLongAxis/kModShortAxis basis
    bool    alongLong;   // true: 43.47 mm side follows u; false: follows v
};

// Lower-z module face: connector-side long inner + short outer tiles.
static const SensorFootprint kLowerFaceSensors[] = {
    {{-13.05f,  4.04f}, true},
    {{ 28.29f,  0.00f}, false},
};

// Upper-z module face: non-connector-side short inner + long outer tiles.
static const SensorFootprint kUpperFaceSensors[] = {
    {{ 14.32f,  0.00f}, false},
    {{-14.05f, -13.77f}, true},
};

// ─── Build from event ────────────────────────────────────────────────────────

void EventScene::Clear() {
    positions_.clear();
    module_.clear();
    ownerPid_.clear();
    ownerCharge_.clear();
    moduleZ0_.clear();
    moduleZ1_.clear();
    hitLayer_ = HitLayer{};
    trackLayers_.clear();
    detector_ = DetectorLayer{};
    bounds_ = BoundingBox{};
}

void EventScene::SetEvent(const velo::VeloEvent& ev) {
    // Preserve per-layer display settings across an event switch. On the first
    // load these are just the constructed defaults, so capturing/reapplying is a
    // no-op; on later loads it keeps the user's customizations.
    struct HitDisplay   { bool visible; float size, alpha; ColorSpec color; };
    struct TrackDisplay { bool visible; float width, alpha; ColorSpec color; };
    const HitDisplay savedHit{hitLayer_.visible, hitLayer_.size, hitLayer_.alpha, hitLayer_.color};
    std::unordered_map<std::string, TrackDisplay> savedTracks;
    for (const auto& l : trackLayers_)
        savedTracks[l.name] = {l.visible, l.width, l.alpha, l.color};
    struct DetDisplay { bool visible; float alpha, thickness, edgeWidth; Color colorA, colorC; };
    const DetDisplay savedDet{detector_.visible, detector_.alpha, detector_.thickness,
                              detector_.edgeWidth, detector_.colorA, detector_.colorC};

    Clear();

    const int n = (int)ev.x.size();
    positions_.resize(n);
    for (int i = 0; i < n; ++i)
        positions_[i] = {(float)ev.x[i], (float)ev.y[i], (float)ev.z[i]};

    // Per-hit module index from the cumulative module_prefix_sum boundaries.
    module_.assign(n, 0);
    {
        const auto& ps = ev.modulePrefixSum;
        int m = 0;
        for (int i = 0; i < n; ++i) {
            while (m + 1 < (int)ps.size() && i >= ps[m + 1]) ++m;
            module_[i] = m;
        }
    }

    // The two sensor-face z-positions per module (connector/non-connector), used
    // to place the two L footprints. Pass 1: module mean z. Pass 2: mean of the
    // hits below / at-or-above that mean. Empty modules get a sentinel.
    {
        const int nmod = std::max(0, (int)ev.modulePrefixSum.size() - 1);
        std::vector<double> zsum(nmod, 0.0);
        std::vector<int>    zcnt(nmod, 0);
        for (int i = 0; i < n; ++i) {
            const int m = module_[i];
            if (m >= 0 && m < nmod) { zsum[m] += positions_[i].z; ++zcnt[m]; }
        }
        std::vector<double> z0s(nmod, 0.0), z1s(nmod, 0.0);
        std::vector<int>    z0c(nmod, 0),   z1c(nmod, 0);
        for (int i = 0; i < n; ++i) {
            const int m = module_[i];
            if (m < 0 || m >= nmod || zcnt[m] == 0) continue;
            const double mean = zsum[m] / zcnt[m];
            if (positions_[i].z < mean) { z0s[m] += positions_[i].z; ++z0c[m]; }
            else                        { z1s[m] += positions_[i].z; ++z1c[m]; }
        }
        moduleZ0_.assign(nmod, 1e30f);
        moduleZ1_.assign(nmod, 1e30f);
        for (int m = 0; m < nmod; ++m) {
            if (zcnt[m] == 0) continue;
            const double mean = zsum[m] / zcnt[m];
            moduleZ0_[m] = (float)(z0c[m] ? z0s[m] / z0c[m] : mean);
            moduleZ1_[m] = (float)(z1c[m] ? z1s[m] / z1c[m] : mean);
        }
    }

    // Per-hit owning particle (first MC particle that references the hit).
    ownerPid_.assign(n, 0);
    ownerCharge_.assign(n, 0);
    for (const auto& p : ev.montecarlo.particles) {
        const int sign = (p.charge > 0) ? 1 : (p.charge < 0 ? -1 : 0);
        for (int h : p.hits)
            if (h >= 0 && h < n && ownerPid_[h] == 0) {
                ownerPid_[h]    = p.pid;
                ownerCharge_[h] = sign;
            }
    }

    // Bounding box.
    if (n > 0) {
        Vector3 mn = positions_[0], mx = positions_[0];
        for (const auto& p : positions_) {
            mn.x = fminf(mn.x, p.x); mx.x = fmaxf(mx.x, p.x);
            mn.y = fminf(mn.y, p.y); mx.y = fmaxf(mx.y, p.y);
            mn.z = fminf(mn.z, p.z); mx.z = fmaxf(mx.z, p.z);
        }
        bounds_ = {mn, mx};
    }

    // Hit layer.
    hitLayer_ = HitLayer{};
    hitLayer_.dirty = true;

    // MC-truth track layer. Default to coloring by eta to showcase the feature.
    TrackLayer mc;
    mc.name = "Tracks";
    mc.color.mode = TCM_Eta;
    mc.tracks.reserve(ev.montecarlo.particles.size());
    mc.meta.reserve(ev.montecarlo.particles.size());
    for (const auto& p : ev.montecarlo.particles) {
        Track t;
        t.hitIndices = p.hits;
        // Order each track's hits along the beam axis so the polyline is clean.
        std::sort(t.hitIndices.begin(), t.hitIndices.end(), [&](int a, int b) {
            if (a < 0 || a >= n) return false;
            if (b < 0 || b >= n) return true;
            return positions_[a].z < positions_[b].z;
        });

        TrackMeta m;
        m.pid        = p.pid;
        m.chargeSign = (p.charge > 0) ? 1 : (p.charge < 0 ? -1 : 0);
        m.p = (float)p.p; m.pt = (float)p.pt; m.eta = (float)p.eta; m.phi = (float)p.phi;
        m.isLong = p.isLong; m.fromBeauty = p.fromBeautyDecay;

        mc.tracks.push_back(std::move(t));
        mc.meta.push_back(m);
    }
    mc.dirty = true;
    trackLayers_.push_back(std::move(mc));

    // Reapply preserved display settings.
    hitLayer_.visible = savedHit.visible;
    hitLayer_.size    = savedHit.size;
    hitLayer_.alpha   = savedHit.alpha;
    hitLayer_.color   = savedHit.color;
    hitLayer_.dirty   = true;

    for (auto& l : trackLayers_) {
        auto it = savedTracks.find(l.name);
        if (it != savedTracks.end()) {
            l.visible = it->second.visible;
            l.width   = it->second.width;
            l.alpha   = it->second.alpha;
            l.color   = it->second.color;
            l.dirty   = true;
        }
    }

    detector_.visible   = savedDet.visible;
    detector_.alpha     = savedDet.alpha;
    detector_.thickness = savedDet.thickness;
    detector_.edgeWidth = savedDet.edgeWidth;
    detector_.colorA    = savedDet.colorA;
    detector_.colorC    = savedDet.colorC;
    detector_.dirty     = true;
}

// ─── Rebuild (bake CPU geometry -> GPU) ────────────────────────────────────────

void EventScene::RebuildHits(HitLayer& l) {
    const int n = (int)positions_.size();
    std::vector<Color> colors(n, l.color.base);

    switch (l.color.mode) {
        case HCM_DepthZ: {  // depth along z
            float mn = 1e30f, mx = -1e30f;
            for (const auto& p : positions_) { mn = fminf(mn, p.z); mx = fmaxf(mx, p.z); }
            if (mx <= mn) mx = mn + 1.0f;
            l.colorRangeMin = mn; l.colorRangeMax = mx;
            if (l.rangeModeCache != HCM_DepthZ || l.rangeLo >= l.rangeHi) {
                l.rangeLo = mn; l.rangeHi = mx; l.rangeModeCache = HCM_DepthZ;
            }
            const float span = fmaxf(l.rangeHi - l.rangeLo, 1e-6f);
            for (int i = 0; i < n; ++i) {
                float t = (positions_[i].z - l.rangeLo) / span;
                t = (t < 0.0f) ? 0.0f : (t > 1.0f) ? 1.0f : t;
                colors[i] = Colormap(t);
            }
        } break;
        case HCM_Module:
            for (int i = 0; i < n; ++i)
                colors[i] = (i < (int)module_.size()) ? Palette(module_[i]) : l.color.base;
            break;
        case HCM_TrackPid:  // owning-track PID (charge-shaded, matching track mode)
            for (int i = 0; i < n; ++i)
                colors[i] = (i < (int)ownerPid_.size() && ownerPid_[i] != 0)
                                ? PidColor(ownerPid_[i], ownerCharge_[i]) : l.color.base;
            break;
        case HCM_TrackAbsPid:  // owning-track |PID|
            for (int i = 0; i < n; ++i)
                colors[i] = (i < (int)ownerPid_.size() && ownerPid_[i] != 0)
                                ? Palette(ownerPid_[i] < 0 ? -ownerPid_[i] : ownerPid_[i]) : l.color.base;
            break;
        case HCM_TrackCharge:  // owning-track charge
            for (int i = 0; i < n; ++i)
                colors[i] = (i < (int)ownerCharge_.size())
                                ? ChargeColor(ownerCharge_[i]) : l.color.base;
            break;
        default: break;  // Uniform: already filled with base
    }

    l.batch.BuildCubes(positions_, colors, l.size);
    l.dirty = false;
}

void EventScene::RebuildTracks(TrackLayer& l) {
    std::vector<Vector3> A, B;
    std::vector<Color>   C;

    // Normalize the selected scalar across the layer for continuous modes, over
    // the user-narrowed [rangeLo, rangeHi] sub-range rather than the full data
    // extent — values outside it clamp to the nearest end colour.
    const bool cont = (l.color.mode >= TCM_Pt && l.color.mode <= TCM_Eta);
    if (cont && !l.meta.empty()) {
        float mn = 1e30f, mx = -1e30f;
        for (const auto& m : l.meta) {
            const float s = TrackScalar(l.color.mode, m);
            mn = fminf(mn, s); mx = fmaxf(mx, s);
        }
        if (mx <= mn) mx = mn + 1.0f;
        l.colorRangeMin = mn; l.colorRangeMax = mx;
        if (l.rangeModeCache != l.color.mode || l.rangeLo >= l.rangeHi) {
            l.rangeLo = mn; l.rangeHi = mx; l.rangeModeCache = l.color.mode;
        }
    }
    const float span = fmaxf(l.rangeHi - l.rangeLo, 1e-6f);

    int segCount = 0;
    for (size_t ti = 0; ti < l.tracks.size(); ++ti) {
        const Track&     t = l.tracks[ti];
        const TrackMeta* m = (ti < l.meta.size()) ? &l.meta[ti] : nullptr;
        float norm = (cont && m) ? (TrackScalar(l.color.mode, *m) - l.rangeLo) / span : 0.0f;
        norm = (norm < 0.0f) ? 0.0f : (norm > 1.0f) ? 1.0f : norm;
        const Color col  = TrackColor(l.color, m, norm);

        for (size_t k = 0; k + 1 < t.hitIndices.size(); ++k) {
            const int i0 = t.hitIndices[k], i1 = t.hitIndices[k + 1];
            if (i0 < 0 || i1 < 0 || i0 >= (int)positions_.size() || i1 >= (int)positions_.size())
                continue;
            A.push_back(positions_[i0]);
            B.push_back(positions_[i1]);
            C.push_back(col);
            ++segCount;
        }
    }

    l.segmentCount = segCount;
    l.batch.BuildSegments(A, B, C, l.width);
    l.dirty = false;
}

void EventScene::RebuildDetector(DetectorLayer& l) {
    std::vector<Vector3> fverts; std::vector<Color> fcols;   // shaded box faces
    std::vector<Vector3> eA, eB; std::vector<Color> ecols;   // wireframe edges

    // Fixed light direction: faces are shaded by |normal . light| so the box's
    // orientation reads even through transparency (both sides lit, culling off).
    const Vector3 Ldir = Vector3Normalize({0.4f, 0.75f, 0.5f});
    const float ht = l.thickness * 0.5f;

    auto quad = [&](Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color base) {
        const Vector3 nrm = Vector3Normalize(
            Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(d, a)));
        const float br = 0.35f + 0.65f * fabsf(Vector3DotProduct(nrm, Ldir));
        const Color col{(unsigned char)(base.r * br), (unsigned char)(base.g * br),
                        (unsigned char)(base.b * br), 255};
        fverts.push_back(a); fverts.push_back(b); fverts.push_back(c);
        fverts.push_back(a); fverts.push_back(c); fverts.push_back(d);
        for (int k = 0; k < 6; ++k) fcols.push_back(col);
    };
    auto edge = [&](Vector3 a, Vector3 b, Color c) { eA.push_back(a); eB.push_back(b); ecols.push_back(c); };

    // One oriented sensor tile: rectangle (centre c, length/width axes,
    // half-extents) extruded between z = zlo and z = zhi, shaded and outlined.
    auto addTile = [&](Vector2 c, Vector2 lenAxis, Vector2 widAxis,
                       float zlo, float zhi, Color base) {
        const Vector2 p[4] = {
            {c.x + kSensorHalfLen * lenAxis.x + kSensorHalfWid * widAxis.x,
             c.y + kSensorHalfLen * lenAxis.y + kSensorHalfWid * widAxis.y},
            {c.x - kSensorHalfLen * lenAxis.x + kSensorHalfWid * widAxis.x,
             c.y - kSensorHalfLen * lenAxis.y + kSensorHalfWid * widAxis.y},
            {c.x - kSensorHalfLen * lenAxis.x - kSensorHalfWid * widAxis.x,
             c.y - kSensorHalfLen * lenAxis.y - kSensorHalfWid * widAxis.y},
            {c.x + kSensorHalfLen * lenAxis.x - kSensorHalfWid * widAxis.x,
             c.y + kSensorHalfLen * lenAxis.y - kSensorHalfWid * widAxis.y},
        };
        Vector3 b[4], t[4];
        for (int i = 0; i < 4; ++i) { b[i] = {p[i].x, p[i].y, zlo}; t[i] = {p[i].x, p[i].y, zhi}; }
        quad(b[0], b[1], b[2], b[3], base);       // bottom
        quad(t[3], t[2], t[1], t[0], base);       // top
        for (int i = 0; i < 4; ++i) { const int j = (i + 1) % 4; quad(b[i], b[j], t[j], t[i], base); }
        for (int i = 0; i < 4; ++i) {             // 12 edges
            const int j = (i + 1) % 4;
            edge(b[i], b[j], base); edge(t[i], t[j], base); edge(b[i], t[i], base);
        }
    };

    auto addFace = [&](const SensorFootprint* sensors, int sensorCount,
                       Vector2 C, Vector2 U, Vector2 V,
                       float z, Color base) {
        for (int i = 0; i < sensorCount; ++i) {
            const SensorFootprint& sensor = sensors[i];
            const Vector2 c{
                C.x + sensor.localCenter.x * U.x + sensor.localCenter.y * V.x,
                C.y + sensor.localCenter.x * U.y + sensor.localCenter.y * V.y,
            };
            addTile(c, sensor.alongLong ? U : V, sensor.alongLong ? V : U,
                    z - ht, z + ht, base);
        }
    };

    int count = 0;
    for (int m = 0; m < (int)moduleZ0_.size(); ++m) {
        const float z0 = moduleZ0_[m], z1 = moduleZ1_[m];
        if (z0 > 1e29f) continue;                 // empty module: skip

        const bool  aSide = (m % 2) != 0;         // odd = positive-x A-side
        const Color col   = aSide ? l.colorA : l.colorC;
        const float s     = aSide ? -1.0f : 1.0f; // A-side = 180 deg rotation about beam

        const Vector2 C {s * kModCenterC.x,   s * kModCenterC.y};
        const Vector2 U {s * kModLongAxis.x,  s * kModLongAxis.y};
        const Vector2 V {s * kModShortAxis.x, s * kModShortAxis.y};

        addFace(kLowerFaceSensors, (int)(sizeof(kLowerFaceSensors) / sizeof(kLowerFaceSensors[0])),
                C, U, V, z0, col);
        addFace(kUpperFaceSensors, (int)(sizeof(kUpperFaceSensors) / sizeof(kUpperFaceSensors[0])),
                C, U, V, z1, col);
        ++count;
    }

    l.moduleCount = count;
    l.batch.BuildTriangles(fverts, fcols);
    l.edgeBatch.BuildSegments(eA, eB, ecols, l.edgeWidth);

    // Keep a CPU copy of the face triangles plus their centroids so Draw() can
    // re-sort them back-to-front against the camera. Invalidate any prior sort.
    l.faceCentroid.resize(fverts.size() / 3);
    for (size_t t = 0; t < l.faceCentroid.size(); ++t)
        l.faceCentroid[t] = Vector3Scale(
            Vector3Add(Vector3Add(fverts[t * 3 + 0], fverts[t * 3 + 1]), fverts[t * 3 + 2]),
            1.0f / 3.0f);
    l.faceVerts = std::move(fverts);
    l.faceCols  = std::move(fcols);
    l.sortValid = false;

    l.dirty = false;
}

// Re-order the detector's translucent face triangles far-to-near from the camera
// so BLEND_ALPHA composites overlapping modules correctly (you see through a near
// module to the ones behind it). Skipped while the camera is essentially still.
void EventScene::SortDetector(Vector3 camPos) {
    DetectorLayer& l = detector_;
    const int tri = (int)l.faceCentroid.size();
    if (tri == 0) return;

    if (l.sortValid && Vector3DistanceSqr(camPos, l.lastSortCam) < 1e-4f)
        return;
    l.lastSortCam = camPos;
    l.sortValid   = true;

    l.triOrder.resize(tri);
    for (int i = 0; i < tri; ++i) l.triOrder[i] = i;
    std::sort(l.triOrder.begin(), l.triOrder.end(), [&](int a, int b) {
        return Vector3DistanceSqr(l.faceCentroid[a], camPos) >
               Vector3DistanceSqr(l.faceCentroid[b], camPos);
    });

    l.sortVerts.resize((size_t)tri * 3);
    l.sortCols.resize((size_t)tri * 3);
    for (int t = 0; t < tri; ++t) {
        const int s = l.triOrder[t];
        for (int k = 0; k < 3; ++k) {
            l.sortVerts[t * 3 + k] = l.faceVerts[s * 3 + k];
            l.sortCols [t * 3 + k] = l.faceCols [s * 3 + k];
        }
    }
    l.batch.UpdateVertices(l.sortVerts, l.sortCols);
}

// ─── Frame ─────────────────────────────────────────────────────────────────────

void EventScene::Update() {
    if (hitLayer_.dirty) RebuildHits(hitLayer_);
    for (auto& l : trackLayers_)
        if (l.dirty) RebuildTracks(l);
    if (detector_.dirty) RebuildDetector(detector_);
}

void EventScene::Draw(Vector3 camPos) {
    // Solid boxes/prisms with arbitrary winding — disable culling so no faces
    // drop out. Alpha blend so per-layer transparency reads correctly.
    rlDisableBackfaceCulling();
    BeginBlendMode(BLEND_ALPHA);

    // Solid content (tracks, then hits) first, with normal depth testing and
    // writing: they occlude one another correctly and seed the depth buffer that
    // the detector overlay is then composited against.
    for (const auto& l : trackLayers_)
        if (l.visible)
            l.batch.Draw({255, 255, 255, (unsigned char)(l.alpha * 255.0f)});

    if (hitLayer_.visible)
        hitLayer_.batch.Draw({255, 255, 255, (unsigned char)(hitLayer_.alpha * 255.0f)});

    // Detector overlay. The translucent module faces overlap heavily in screen
    // space, so correct transparency needs them composited back-to-front: SortDetector
    // re-orders the baked triangles by camera distance each frame (cheap; skipped
    // while the camera is still). Depth *writes* stay off — the faint overlay must
    // not hide the hits/tracks — but depth *testing* stays on so solid geometry in
    // front of a face still occludes it. With the faces ordered far→near, a nearer
    // module correctly blends over (and shows through to) the ones behind it.
    if (detector_.visible && !detector_.batch.Empty()) {
        SortDetector(camPos);
        rlDisableDepthMask();
        detector_.batch.Draw({255, 255, 255, (unsigned char)(detector_.alpha * 255.0f)});
        // Outlines stay more opaque than the faces so the box shape reads clearly.
        const float ea = fminf(1.0f, detector_.alpha * 1.6f + 0.35f);
        detector_.edgeBatch.Draw({255, 255, 255, (unsigned char)(ea * 255.0f)});
        rlEnableDepthMask();
    }

    EndBlendMode();
    rlEnableBackfaceCulling();
}

// ─── Inspector UI ──────────────────────────────────────────────────────────────

static bool BaseColorEdit(ColorSpec& cs) {
    float c[3] = {cs.base.r / 255.0f, cs.base.g / 255.0f, cs.base.b / 255.0f};
    if (ImGui::ColorEdit3("Base color", c)) {
        cs.base = {(unsigned char)(c[0] * 255), (unsigned char)(c[1] * 255),
                   (unsigned char)(c[2] * 255), 255};
        return true;
    }
    return false;
}

static bool RawColorEdit(const char* label, Color& col) {
    float c[3] = {col.r / 255.0f, col.g / 255.0f, col.b / 255.0f};
    if (ImGui::ColorEdit3(label, c)) {
        col = {(unsigned char)(c[0] * 255), (unsigned char)(c[1] * 255),
               (unsigned char)(c[2] * 255), 255};
        return true;
    }
    return false;
}

// Friendly names for common PDG codes (|pid|); unrecognized species just show
// their numeric code. Not exhaustive by design — the legend falls back cleanly.
static const char* PidName(int absPid) {
    switch (absPid) {
        case 11:   return "e";
        case 13:   return "mu";
        case 15:   return "tau";
        case 22:   return "gamma";
        case 111:  return "pi0";
        case 211:  return "pi";
        case 130:  return "K_L";
        case 310:  return "K_S";
        case 321:  return "K";
        case 2112: return "n";
        case 2212: return "p";
        case 3122: return "Lambda";
        case 3112: return "Sigma-";
        case 3222: return "Sigma+";
        case 411:  return "D";
        case 421:  return "D0";
        case 431:  return "D_s";
        case 511:  return "B0";
        case 521:  return "B";
        case 531:  return "B_s";
        case 443:  return "J/psi";
        default:   return nullptr;
    }
}

// Distinct |PID| species referenced by [begin, end), in ascending order.
// pidOf extracts the (possibly signed) PID from each element; zero (= no
// owning particle / unset) is skipped.
template <typename It, typename PidOf>
static std::vector<int> UniqueAbsPids(It begin, It end, PidOf pidOf) {
    std::vector<int> out;
    for (It it = begin; it != end; ++it) {
        const int pid = pidOf(*it);
        if (pid == 0) continue;
        const int a = (pid < 0) ? -pid : pid;
        if (std::find(out.begin(), out.end(), a) == out.end())
            out.push_back(a);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static const ImGuiColorEditFlags kSwatchFlags = ImGuiColorEditFlags_NoTooltip |
                                                 ImGuiColorEditFlags_NoPicker |
                                                 ImGuiColorEditFlags_NoAlpha;

static void ColorSwatch(const char* id, Color col) {
    ImGui::ColorButton(id, ImVec4(col.r / 255.0f, col.g / 255.0f, col.b / 255.0f, 1.0f),
                        kSwatchFlags, ImVec2(14, 14));
}

// Colour-swatch legend rows for one PID-coloured source: one row per species,
// labelled with a friendly name where known. Signed PID modes shade the base
// hue by charge, so those get two explicit swatches per row (left = positive,
// right = negative) instead of a single neutral one plus a text explanation.
static void DrawPidLegendRows(const std::vector<int>& absPids, bool signedShading) {
    for (int pid : absPids) {
        ImGui::PushID(pid);
        if (signedShading) {
            ColorSwatch("##pos", PidColor(pid, 1));
            ImGui::SameLine(0.0f, 2.0f);
            ColorSwatch("##neg", PidColor(pid, -1));
        } else {
            ColorSwatch("##swatch", Palette(pid));
        }
        ImGui::SameLine();
        if (const char* name = PidName(pid))
            ImGui::Text("%s (%d)", name, pid);
        else
            ImGui::Text("%d", pid);
        ImGui::PopID();
    }
}

// Colour-swatch legend for the charge-sign colouring: one row per sign,
// covering the three ChargeColor() outcomes.
static void DrawChargeLegendRows() {
    static const struct { int sign; const char* label; } kRows[] = {
        {1, "positive"}, {-1, "negative"}, {0, "neutral"},
    };
    for (const auto& row : kRows) {
        ImGui::PushID(row.sign);
        ColorSwatch("##swatch", ChargeColor(row.sign));
        ImGui::SameLine();
        ImGui::Text("%s", row.label);
        ImGui::PopID();
    }
}

// Wide interactive colour-scale bar for a continuous mode (Depth z / pT / p /
// eta): a smooth gradient swept through Colormap() over the full data range
// [dataMin, dataMax], with two draggable triangle handles marking the
// [lo, hi] sub-range the colour scale is actually stretched over — dragging a
// handle narrows the range, clamping values beyond it to the end colour.
// Returns true the frame either handle moved (caller must then re-bake).
static bool DrawColormapRangeBar(const char* propName, float dataMin, float dataMax, float& lo, float& hi) {
    bool changed = false;
    ImGui::Text("%s", propName);

    const float barW = 340.0f, barH = 28.0f;     // the gradient itself
    const float hitW = 22.0f, hitH = 22.0f;       // draggable hit-box, bigger than the drawn triangle
    const float triW = 16.0f, triH = 16.0f;       // drawn triangle, centred in the hit-box
    ImDrawList*   dl = ImGui::GetWindowDrawList();
    const ImVec2  p0 = ImGui::GetCursorScreenPos();

    const int steps = 64;
    for (int i = 0; i < steps; ++i) {
        const Color c = Colormap((float)i / steps);
        const float x0 = p0.x + barW * i / steps;
        const float x1 = p0.x + barW * (i + 1) / steps;
        dl->AddRectFilled(ImVec2(x0, p0.y), ImVec2(x1, p0.y + barH), IM_COL32(c.r, c.g, c.b, 255));
    }
    dl->AddRect(p0, ImVec2(p0.x + barW, p0.y + barH), IM_COL32(255, 255, 255, 80));
    ImGui::Dummy(ImVec2(barW, barH));

    const float range   = fmaxf(dataMax - dataMin, 1e-6f);
    const float epsilon = fmaxf(range * 0.002f, 1e-5f);
    auto valueToX = [&](float v) { return p0.x + barW * (v - dataMin) / range; };

    const ImVec2 rowPos = ImGui::GetCursorScreenPos();

    // The hit-box is clamped to stay fully within [p0.x, p0.x + barW] so it
    // never spills past the bar's edges — letting it do so at the extremes
    // (lo == dataMin or hi == dataMax) would push the window's auto-fit
    // bounds outward and make the whole legend visibly resize while dragging.
    auto drawHandle = [&](const char* id, float& value, float minBound, float maxBound) {
        float bx = valueToX(value) - hitW * 0.5f;
        bx = (bx < p0.x) ? p0.x : (bx > p0.x + barW - hitW) ? p0.x + barW - hitW : bx;
        ImGui::SetCursorScreenPos(ImVec2(bx, rowPos.y));
        ImGui::InvisibleButton(id, ImVec2(hitW, hitH));
        const bool active  = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (active && ImGui::GetIO().MouseDelta.x != 0.0f) {
            float nv = value + ImGui::GetIO().MouseDelta.x / barW * range;
            nv = (nv < minBound) ? minBound : (nv > maxBound) ? maxBound : nv;
            if (nv != value) { value = nv; changed = true; }
        }
        const ImU32 col = active ? IM_COL32(255, 255, 255, 255)
                         : hovered ? IM_COL32(225, 225, 225, 255)
                                   : IM_COL32(190, 190, 190, 255);
        const float cx = bx + hitW * 0.5f;  // triangle follows the clamped, on-screen centre
        dl->AddTriangleFilled(ImVec2(cx - triW * 0.5f, rowPos.y), ImVec2(cx + triW * 0.5f, rowPos.y),
                              ImVec2(cx, rowPos.y + triH), col);
    };

    drawHandle("##lo", lo, dataMin, hi - epsilon);
    drawHandle("##hi", hi, lo + epsilon, dataMax);
    ImGui::Dummy(ImVec2(barW, hitH));

    // lo left-aligned at x=0, hi right-aligned to end exactly at x=barW (using
    // its own measured width, not a guessed one) so the row's content width
    // never exceeds the bar regardless of how many digits either value has.
    char hiText[32];
    std::snprintf(hiText, sizeof(hiText), "%.2f", hi);
    ImGui::Text("%.2f", lo);
    ImGui::SameLine(barW - ImGui::CalcTextSize(hiText).x);
    ImGui::Text("%s", hiText);

    return changed;
}

// Small floating window docked to the top-right, shown only while at least one
// visible layer is actually colored by PID / |PID| / charge — kept out of the
// main control panel so it doesn't take up space the rest of the time.
void EventScene::DrawPidLegendWindow() {
    enum class Kind { Pid, Charge };
    struct Source { std::string label; Kind kind; std::vector<int> absPids; bool signedShading; };
    std::vector<Source> sources;

    if (hitLayer_.visible) {
        if (hitLayer_.color.mode == HCM_TrackPid || hitLayer_.color.mode == HCM_TrackAbsPid) {
            auto pids = UniqueAbsPids(ownerPid_.begin(), ownerPid_.end(), [](int p) { return p; });
            if (!pids.empty())
                sources.push_back({"Hits", Kind::Pid, std::move(pids), hitLayer_.color.mode == HCM_TrackPid});
        } else if (hitLayer_.color.mode == HCM_TrackCharge) {
            sources.push_back({"Hits", Kind::Charge, {}, false});
        }
    }
    for (const auto& l : trackLayers_) {
        if (!l.visible) continue;
        if (l.color.mode == TCM_Pid || l.color.mode == TCM_AbsPid) {
            auto pids = UniqueAbsPids(l.meta.begin(), l.meta.end(), [](const TrackMeta& m) { return m.pid; });
            if (!pids.empty())
                sources.push_back({l.name, Kind::Pid, std::move(pids), l.color.mode == TCM_Pid});
        } else if (l.color.mode == TCM_Charge) {
            sources.push_back({l.name, Kind::Charge, {}, false});
        }
    }
    if (!sources.empty()) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.0f, 10.0f), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowBgAlpha(0.85f);
        const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                       ImGuiWindowFlags_NoFocusOnAppearing;
        if (ImGui::Begin("Legend", nullptr, flags)) {
            for (size_t i = 0; i < sources.size(); ++i) {
                if (i > 0) ImGui::Spacing();
                if (sources.size() > 1) ImGui::TextDisabled("%s", sources[i].label.c_str());
                if (sources[i].kind == Kind::Pid)
                    DrawPidLegendRows(sources[i].absPids, sources[i].signedShading);
                else
                    DrawChargeLegendRows();
            }
        }
        ImGui::End();
    }

    DrawColorbarWindow();
}

// Small floating window docked to the bottom-right, shown only while at least
// one visible layer is colored by a continuous property (Depth z / pT / p /
// eta). Kept separate from the PID/charge legend so its gradient bar has room
// to be readable instead of being squeezed into a narrow shared panel.
void EventScene::DrawColorbarWindow() {
    const bool hitsActive = hitLayer_.visible && hitLayer_.color.mode == HCM_DepthZ;
    int activeCount = hitsActive ? 1 : 0;
    for (const auto& l : trackLayers_)
        if (l.visible && (l.color.mode == TCM_Pt || l.color.mode == TCM_P || l.color.mode == TCM_Eta))
            ++activeCount;
    if (activeCount == 0) return;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 10.0f, io.DisplaySize.y - 10.0f),
                            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.9f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoFocusOnAppearing;
    if (ImGui::Begin("Colorbar", nullptr, flags)) {
        bool first = true;
        if (hitsActive) {
            first = false;
            if (activeCount > 1) ImGui::TextDisabled("Hits");
            ImGui::PushID("hits_colorbar");
            if (DrawColormapRangeBar("Depth (z)", hitLayer_.colorRangeMin, hitLayer_.colorRangeMax,
                                     hitLayer_.rangeLo, hitLayer_.rangeHi))
                hitLayer_.dirty = true;
            ImGui::PopID();
        }
        for (auto& l : trackLayers_) {
            if (!l.visible || (l.color.mode != TCM_Pt && l.color.mode != TCM_P && l.color.mode != TCM_Eta)) continue;
            if (!first) ImGui::Spacing();
            first = false;
            if (activeCount > 1) ImGui::TextDisabled("%s", l.name.c_str());
            const char* propName = (l.color.mode == TCM_Pt) ? "pT" : (l.color.mode == TCM_P) ? "p" : "eta";
            ImGui::PushID(l.name.c_str());
            if (DrawColormapRangeBar(propName, l.colorRangeMin, l.colorRangeMax, l.rangeLo, l.rangeHi))
                l.dirty = true;
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void EventScene::DrawInspectorUI() {
    // ── Hits ──
    if (ImGui::CollapsingHeader("Hits", ImGuiTreeNodeFlags_DefaultOpen)) {
        HitLayer& l = hitLayer_;
        ImGui::PushID("hits");
        ImGui::Checkbox("Visible", &l.visible);
        ImGui::SameLine();
        ImGui::TextDisabled("(%d)", (int)positions_.size());
        if (ImGui::Combo("Color by", &l.color.mode, kHitModes, IM_ARRAYSIZE(kHitModes)))
            l.dirty = true;
        if (l.color.mode == 0 && BaseColorEdit(l.color)) l.dirty = true;
        if (ImGui::SliderFloat("Size", &l.size, 0.3f, 5.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
            l.dirty = true;
        ImGui::SliderFloat("Alpha", &l.alpha, 0.0f, 1.0f, "%.2f");  // live, no rebuild
        ImGui::PopID();
    }

    // ── Track layers ──
    for (size_t i = 0; i < trackLayers_.size(); ++i) {
        TrackLayer& l = trackLayers_[i];
        ImGui::PushID((int)(1000 + i));
        if (ImGui::CollapsingHeader(l.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Visible", &l.visible);
            ImGui::SameLine();
            ImGui::TextDisabled("(%d tracks, %d segs)", (int)l.tracks.size(), l.segmentCount);
            if (ImGui::Combo("Color by", &l.color.mode, kTrackModes, IM_ARRAYSIZE(kTrackModes)))
                l.dirty = true;
            if (l.color.mode == 0 && BaseColorEdit(l.color)) l.dirty = true;
            if (ImGui::SliderFloat("Width", &l.width, 0.1f, 3.0f, "%.2f", ImGuiSliderFlags_Logarithmic))
                l.dirty = true;
            ImGui::SliderFloat("Alpha", &l.alpha, 0.0f, 1.0f, "%.2f");  // live, no rebuild
        }
        ImGui::PopID();
    }

    // ── VELO detector ──
    {
        DetectorLayer& l = detector_;
        ImGui::PushID("detector");
        if (ImGui::CollapsingHeader(l.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Visible", &l.visible);
            ImGui::SameLine();
            ImGui::TextDisabled("(%d modules)", l.moduleCount);
            if (RawColorEdit("A-side", l.colorA)) l.dirty = true;
            if (RawColorEdit("C-side", l.colorC)) l.dirty = true;
            ImGui::SliderFloat("Alpha", &l.alpha, 0.0f, 1.0f, "%.2f");  // live, no rebuild
            if (ImGui::SliderFloat("Thickness", &l.thickness, 0.5f, 5.0f, "%.1f mm"))
                l.dirty = true;
            if (ImGui::SliderFloat("Edge width", &l.edgeWidth, 0.0f, 1.0f, "%.2f mm",
                                   ImGuiSliderFlags_Logarithmic))
                l.dirty = true;
        }
        ImGui::PopID();
    }
}
