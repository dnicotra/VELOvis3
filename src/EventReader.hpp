#pragma once
#include <string>
#include <vector>

// Data model + loader for the VELO event JSON files under assets/events/.
// Each file holds one LHCb-VELO-style event: reconstructed hit positions
// (x/y/z, module-indexed via module_prefix_sum) plus Monte Carlo truth
// (one entry per generated particle, referencing hits by index).
namespace velo {

struct MCParticle {
    int key   = 0;
    int pid   = 0;
    double p = 0, pt = 0, eta = 0, phi = 0;
    bool isLong = false, isDown = false;
    bool hasVelo = false, hasUT = false, hasScifi = false;
    bool fromBeautyDecay = false, fromCharmDecay = false, fromStrangeDecay = false;
    double charge = 0;
    std::vector<int> hits; // indices into VeloEvent::x/y/z
};

struct MonteCarlo {
    std::vector<std::string> description; // field names, in on-disk order
    std::vector<MCParticle> particles;
};

struct VeloEvent {
    std::string description;
    std::vector<int> modulePrefixSum; // hit-count offset per module
    MonteCarlo montecarlo;
    std::vector<double> x, y, z; // hit positions, parallel arrays
};

class EventReader {
public:
    // Throws std::runtime_error if the file can't be opened or parsed.
    static VeloEvent LoadFromFile(const std::string& path);

    // Throws std::runtime_error if jsonText is not valid or doesn't match
    // the expected event schema.
    static VeloEvent LoadFromString(const std::string& jsonText);
};

} // namespace velo
