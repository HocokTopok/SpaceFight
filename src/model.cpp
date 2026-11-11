#include "gd/model.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>
#include <stdexcept>

namespace gd
{
bool Resources::covers(Resources c) const
{
    return metal >= c.metal && energy >= c.energy && science >= c.science;
}
Resources &Resources::operator+=(Resources r)
{
    metal += r.metal;
    energy += r.energy;
    science += r.science;
    return *this;
}
Resources &Resources::operator-=(Resources r)
{
    metal -= r.metal;
    energy -= r.energy;
    science -= r.science;
    return *this;
}
const std::array<ShipSpec, 4> &shipSpecs()
{
    static const std::array<ShipSpec, 4> specs{{{"Fighter", 8, 12, 150, {14, 6, 0}, 1},
                                                {"Frigate", 18, 35, 110, {30, 12, 0}, 2},
                                                {"Cruiser", 48, 100, 80, {80, 35, 0}, 3},
                                                {"Bomber", 6, 24, 100, {28, 15, 0}, 2}}};
    return specs;
}
const std::array<BuildingSpec, 6> &buildingSpecs()
{
    static const std::array<BuildingSpec, 6> specs{{{"Mine", {35, 10, 0}, 2, {12, 0, 0}, 2},
                                                    {"Power plant", {30, 5, 0}, 2, {0, 16, 0}, 0},
                                                    {"Laboratory", {40, 20, 0}, 2, {0, 0, 10}, 3},
                                                    {"Shipyard", {55, 20, 0}, 3, {}, 3},
                                                    {"Defense station", {40, 25, 0}, 2, {}, 2},
                                                    {"Radar", {25, 20, 0}, 1, {}, 1}}};
    return specs;
}
const std::array<TechSpec, 9> &techSpecs()
{
    static const std::array<TechSpec, 9> specs{{{"Extraction", "+30% metal production", 35, -1},
                                                {"Automation", "Construction and ships: -1 turn", 60, 0},
                                                {"Efficiency", "Metal and energy costs: -20%", 85, 1},
                                                {"Weapons", "+25% ship attack", 35, -1},
                                                {"Armor", "+30% ship armor", 60, 3},
                                                {"Heavy ships", "Unlock cruisers and bombers", 85, 4},
                                                {"Engines", "+40% fleet speed", 35, -1},
                                                {"Deep sensors", "Reveal fleets two lanes away", 60, 6},
                                                {"Warp drive", "+60% speed, +5 energy per colony", 85, 7}}};
    return specs;
}
std::string planetTypeName(PlanetType t)
{
    static const std::array<std::string, 5> names{"Terran", "Desert", "Ice", "Volcanic", "Gas giant"};
    return names.at(static_cast<int>(t));
}
int shipCount(const Ships &s)
{
    return std::accumulate(s.begin(), s.end(), 0);
}
Planet PlanetBuilder::make(int id, std::string name, PlanetType type, float x, float y)
{
    Planet p;
    p.id = id;
    p.name = std::move(name);
    p.type = type;
    p.x = x;
    p.y = y;
    set_economy(p);
    set_colony(p);
    return p;
}
static Resources baseProduction(PlanetType t)
{
    static const std::array<Resources, 5> values{{{6, 6, 3}, {10, 3, 1}, {3, 4, 7}, {5, 12, 1}, {2, 16, 2}}};
    return values.at(static_cast<int>(t));
}
void FrontierBuilder::set_economy(Planet &p) const
{
    p.production = baseProduction(p.type);
    p.slots = p.type == PlanetType::Gas ? 4 : 6;
    p.radius = p.type == PlanetType::Gas ? 29 : 22;
}
void FrontierBuilder::set_colony(Planet &p) const
{
    p.defense = 10 + (p.id % 3) * 4;
}
void CapitalBuilder::set_economy(Planet &p) const
{
    p.production = {10, 10, 5};
    p.slots = 9;
    p.radius = 30;
}
void CapitalBuilder::set_colony(Planet &p) const
{
    p.owner = m_owner;
    p.capital = m_owner;
    p.buildings = {1, 1, 1, 1, 1, 0};
    p.ships = {7, 2, 0, 0};
    p.defense = 30;
}
FleetStats Ship::stats() const
{
    const auto &s = shipSpecs().at(static_cast<int>(m_type));
    return {s.attack, s.armor, s.speed, 1, m_type == ShipType::Bomber ? 38 : 0};
}
void FleetGroup::add(std::unique_ptr<FleetComponent> c)
{
    if (!c)
        throw std::invalid_argument("Null fleet component");
    m_components.push_back(std::move(c));
}
FleetStats FleetGroup::stats() const
{
    FleetStats result;
    for (const auto &c : m_components)
    {
        const auto s = c->stats();
        result.attack += s.attack;
        result.armor += s.armor;
        result.count += s.count;
        result.siege += s.siege;
        result.speed = std::min(result.speed, s.speed);
    }
    return result;
}
FleetStats ShipUpgrade::stats() const
{
    auto s = m_component->stats();
    if (m_tech == Tech::Weapons)
        s.attack += std::max(1, s.attack / 4);
    if (m_tech == Tech::Armor)
        s.armor += s.armor * 3 / 10;
    if (m_tech == Tech::Engines)
        s.speed += s.speed * 4 / 10;
    if (m_tech == Tech::Warp)
        s.speed += s.speed * 6 / 10;
    return s;
}
std::unique_ptr<FleetComponent> makeFleet(const Ships &ships, const Technologies &tech)
{
    const TypedShipFactory<ShipType::Fighter> fighter;
    const TypedShipFactory<ShipType::Frigate> frigate;
    const TypedShipFactory<ShipType::Cruiser> cruiser;
    const TypedShipFactory<ShipType::Bomber> bomber;
    const std::array<const ShipFactory *, 4> factories{&fighter, &frigate, &cruiser, &bomber};
    auto armada = std::make_unique<FleetGroup>();
    for (int type = 0; type < 4; ++type)
    {
        auto squadron = std::make_unique<FleetGroup>();
        for (int i = 0; i < ships[type]; ++i)
        {
            auto ship = factories[type]->make_ship();
            for (int t : {3, 4, 6, 8})
                if (tech[t])
                    ship = std::make_unique<ShipUpgrade>(std::move(ship), static_cast<Tech>(t));
            squadron->add(std::move(ship));
        }
        if (ships[type])
            armada->add(std::move(squadron));
    }
    return armada;
}
World makeWorld(std::uint32_t seed)
{
    World w;
    w.seed = seed;
    std::mt19937 rng(seed);
    const std::array<std::string, 20> names{"Sol",    "Vega",  "Nyx",   "Aster",  "Eos",   "Mira",   "Io",
                                            "Kepler", "Lyra",  "Orion", "Altair", "Ceres", "Rhea",   "Helios",
                                            "Nova",   "Titan", "Atlas", "Sirius", "Nox",   "Antares"};
    FrontierBuilder frontier;
    std::array<int, 10> types{0, 1, 2, 3, 4, 0, 1, 2, 3, 4};
    std::shuffle(types.begin(), types.end(), rng);
    for (int id = 0; id < 10; ++id)
    {
        int col = id % 5, row = id / 5;
        float x = 85.f + col * 165.f + static_cast<int>(rng() % 27) - 13.f;
        float y = 105.f + row * 145.f + static_cast<int>(rng() % 23) - 11.f;
        auto type = static_cast<PlanetType>(types[id]);
        w.planets.push_back(frontier.make(id, names[id], type, x, y));
    }
    for (int id = 10; id < 20; ++id)
    {
        const auto p = w.planets[19 - id];
        w.planets.push_back(frontier.make(id, names[id], p.type, 830.f - p.x, 650.f - p.y));
        w.planets.back().defense = p.defense;
    }
    for (int i = 0; i < 20; ++i)
    {
        if (i % 5 < 4)
            w.lanes.push_back({i, i + 1});
        if (i < 15)
            w.lanes.push_back({i, i + 5});
    }
    for (int owner = 0; owner < 2; ++owner)
    {
        int id = owner == 0 ? 0 : 19;
        auto p = w.planets[id];
        w.planets[id] = CapitalBuilder(owner).make(id, names[id], PlanetType::Terran, p.x, p.y);
    }
    return w;
}
bool linked(const World &w, int a, int b)
{
    return std::ranges::any_of(w.lanes,
                               [=](auto e) { return (e[0] == a && e[1] == b) || (e[1] == a && e[0] == b); });
}
Resources income(const Planet &p, const Player &player)
{
    auto r = p.production;
    for (int b = 0; b < 6; ++b)
        for (int n = 0; n < p.buildings[b]; ++n)
        {
            r += buildingSpecs()[b].production;
            r.energy -= buildingSpecs()[b].upkeep;
        }
    if (player.tech[0])
        r.metal += r.metal * 3 / 10;
    if (player.tech[8])
        r.energy += 5;
    return r;
}
Resources price(Resources r, const Player &p)
{
    if (p.tech[2])
    {
        r.metal = (r.metal * 4 + 4) / 5;
        r.energy = (r.energy * 4 + 4) / 5;
    }
    return r;
}
bool visible(const World &w, int player, int target)
{
    if (player < 0 || player > 1 || target < 0 || target >= static_cast<int>(w.planets.size()))
        return false;
    for (const auto &p : w.planets)
        if (p.owner == player)
        {
            if (p.id == target || linked(w, p.id, target))
                return true;
            if (p.buildings[5] || w.players[player].tech[7])
                for (const auto &q : w.planets)
                    if (linked(w, p.id, q.id) && linked(w, q.id, target))
                        return true;
        }
    return false;
}
int travelTime(const World &w, int player, int from, int to, const Ships &ships)
{
    const auto &a = w.planets.at(from);
    const auto &b = w.planets.at(to);
    const int speed = makeFleet(ships, w.players.at(player).tech)->stats().speed;
    return std::max(1, static_cast<int>(std::ceil(std::hypot(a.x - b.x, a.y - b.y) / speed)));
}
} // namespace gd
