#include "EventReader.hpp"

#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

using json = nlohmann::json;

namespace velo {

namespace {

// Maps montecarlo.description field names to their index within each
// particle's value array, so particle rows are read by name rather than by
// an assumed fixed column order.
using FieldIndex = std::unordered_map<std::string, size_t>;

FieldIndex BuildFieldIndex(const std::vector<std::string>& fields) {
    FieldIndex index;
    for (size_t i = 0; i < fields.size(); ++i)
        index[fields[i]] = i;
    return index;
}

const json& Field(const json& particleRow, const FieldIndex& index, const char* name) {
    auto it = index.find(name);
    if (it == index.end())
        throw std::runtime_error(std::string("missing montecarlo field: ") + name);
    return particleRow.at(it->second);
}

bool FieldAsBool(const json& particleRow, const FieldIndex& index, const char* name) {
    return Field(particleRow, index, name).get<int>() != 0;
}

MCParticle ParseParticle(const json& row, const FieldIndex& index) {
    MCParticle p;
    p.key   = Field(row, index, "key").get<int>();
    p.pid   = Field(row, index, "pid").get<int>();
    p.p     = Field(row, index, "p").get<double>();
    p.pt    = Field(row, index, "pt").get<double>();
    p.eta   = Field(row, index, "eta").get<double>();
    p.phi   = Field(row, index, "phi").get<double>();
    p.isLong = FieldAsBool(row, index, "isLong");
    p.isDown = FieldAsBool(row, index, "isDown");
    p.hasVelo = FieldAsBool(row, index, "hasVelo");
    p.hasUT   = FieldAsBool(row, index, "hasUT");
    p.hasScifi = FieldAsBool(row, index, "hasScifi");
    p.fromBeautyDecay  = FieldAsBool(row, index, "fromBeautyDecay");
    p.fromCharmDecay   = FieldAsBool(row, index, "fromCharmDecay");
    p.fromStrangeDecay = FieldAsBool(row, index, "fromStrangeDecay");
    p.charge = Field(row, index, "charge").get<double>();
    p.hits   = Field(row, index, "hits").get<std::vector<int>>();
    return p;
}

MonteCarlo ParseMonteCarlo(const json& mc) {
    MonteCarlo out;
    out.description = mc.at("description").get<std::vector<std::string>>();
    const FieldIndex index = BuildFieldIndex(out.description);

    const json& particles = mc.at("particles");
    out.particles.reserve(particles.size());
    for (const json& row : particles)
        out.particles.push_back(ParseParticle(row, index));
    return out;
}

VeloEvent ParseEvent(const json& root) {
    VeloEvent event;
    event.description     = root.at("description").get<std::string>();
    event.modulePrefixSum = root.at("module_prefix_sum").get<std::vector<int>>();
    event.montecarlo      = ParseMonteCarlo(root.at("montecarlo"));
    event.x = root.at("x").get<std::vector<double>>();
    event.y = root.at("y").get<std::vector<double>>();
    event.z = root.at("z").get<std::vector<double>>();

    if (event.x.size() != event.y.size() || event.x.size() != event.z.size())
        throw std::runtime_error("VELO event hit arrays x/y/z have mismatched lengths");

    return event;
}

} // namespace

VeloEvent EventReader::LoadFromString(const std::string& jsonText) {
    json root;
    try {
        root = json::parse(jsonText);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("failed to parse VELO event JSON: ") + e.what());
    }

    try {
        return ParseEvent(root);
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("malformed VELO event JSON: ") + e.what());
    }
}

VeloEvent EventReader::LoadFromFile(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("could not open VELO event file: " + path);

    std::ostringstream buffer;
    buffer << file.rdbuf();

    try {
        return LoadFromString(buffer.str());
    } catch (const std::runtime_error& e) {
        throw std::runtime_error(path + ": " + e.what());
    }
}

} // namespace velo
