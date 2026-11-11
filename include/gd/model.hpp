#pragma once
#include <array>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>

namespace gd
{
constexpr int neutral = -1;
struct Resources
{
    int metal = 0, energy = 0, science = 0;
    bool covers(Resources cost) const;
    Resources & operator+=(Resources rhs);
    Resources & operator-=(Resources rhs);
    bool operator==(const Resources &) const = default;
};
enum class PlanetType { Terran, Desert, Ice, Volcanic, Gas };
enum class Building { Mine, PowerPlant, Laboratory, Shipyard, DefenseStation, Radar };
enum class ShipType { Fighter, Frigate, Cruiser, Bomber };
enum class Tech { Extraction, Automation, Efficiency, Weapons, Armor, HeavyShips, Engines, Sensors, Warp };
using Ships = std::array<int, 4>;
using Technologies = std::array<bool, 9>;
struct ShipSpec { std::string name; int attack, armor, speed; Resources cost; int turns; };
struct BuildingSpec { std::string name; Resources cost; int turns; Resources production; int upkeep; };
struct TechSpec { std::string name, description; int cost, prerequisite; };
const std::array<ShipSpec, 4> & shipSpecs();
const std::array<BuildingSpec, 6> & buildingSpecs();
const std::array<TechSpec, 9> & techSpecs();
std::string planetTypeName(PlanetType type);
int shipCount(const Ships & ships);
struct Job { bool ship = false; int type = 0, remaining = 0; };
struct Planet
{
    int id = 0;
    std::string name;
    PlanetType type = PlanetType::Terran;
    float x = 0, y = 0, radius = 24;
    int owner = neutral, capital = neutral, slots = 6, defense = 10;
    Resources production;
    std::array<int, 6> buildings{};
    Ships ships{};
    std::vector<Job> jobs;
};
struct Player
{
    Resources resources{140, 100, 30};
    Technologies tech{};
    int research = -1, progress = 0;
    bool shortage = false;
};
struct Transit
{
    int owner = 0, from = 0, to = 0, remaining = 1, duration = 1;
    Ships ships{};
};
struct World
{
    std::uint32_t seed = 42;
    int turn = 1, winner = -1;
    std::array<Player, 2> players;
    std::vector<Planet> planets;
    std::vector<std::array<int, 2>> lanes;
    std::vector<Transit> transits;
};

///////////////////////////////////////////////////////////////////////////////
// Pattern Builder
///////////////////////////////////////////////////////////////////////////////
class PlanetBuilder
{
public :
    virtual ~PlanetBuilder() = default;
    Planet make(int id, std::string name, PlanetType type, float x, float y);
private :
    virtual void set_economy(Planet & planet) const = 0;
    virtual void set_colony(Planet & planet) const = 0;
};
class FrontierBuilder : public PlanetBuilder
{
private :
    void set_economy(Planet & planet) const override;
    void set_colony(Planet & planet) const override;
};
class CapitalBuilder : public PlanetBuilder
{
public :
    explicit CapitalBuilder(int owner) : m_owner(owner) {}
private :
    void set_economy(Planet & planet) const override;
    void set_colony(Planet & planet) const override;
    int m_owner;
};
///////////////////////////////////////////////////////////////////////////////
struct FleetStats
{
    int attack = 0, armor = 0, speed = 1000, count = 0, siege = 0;
};
///////////////////////////////////////////////////////////////////////////////
// Pattern Composite
///////////////////////////////////////////////////////////////////////////////
class FleetComponent
{
public :
    virtual ~FleetComponent() = default;
    virtual FleetStats stats() const = 0;
};
class Ship : public FleetComponent
{
public :
    explicit Ship(ShipType type) : m_type(type) {}
    FleetStats stats() const override;
private :
    ShipType m_type;
};
class FleetGroup : public FleetComponent
{
public :
    void add(std::unique_ptr<FleetComponent> component);
    FleetStats stats() const override;
private :
    std::vector<std::unique_ptr<FleetComponent>> m_components;
};
///////////////////////////////////////////////////////////////////////////////
// Pattern Factory Method
///////////////////////////////////////////////////////////////////////////////
class ShipFactory
{
public :
    virtual ~ShipFactory() = default;
    virtual std::unique_ptr<FleetComponent> make_ship() const = 0;
};
template < ShipType Type > class TypedShipFactory : public ShipFactory
{
public :
    std::unique_ptr<FleetComponent> make_ship() const override
    {
        return std::make_unique<Ship>(Type);
    }
};
///////////////////////////////////////////////////////////////////////////////
// Pattern Decorator
///////////////////////////////////////////////////////////////////////////////
class ShipUpgrade : public FleetComponent
{
public :
    ShipUpgrade(std::unique_ptr<FleetComponent> component, Tech tech)
        : m_component(std::move(component)), m_tech(tech) {}
    FleetStats stats() const override;
private :
    std::unique_ptr<FleetComponent> m_component;
    Tech m_tech;
};
///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<FleetComponent> makeFleet(const Ships & ships, const Technologies & tech);
World makeWorld(std::uint32_t seed = 42);
bool linked(const World & world, int from, int to);
Resources income(const Planet & planet, const Player & player);
Resources price(Resources base, const Player & player);
bool visible(const World & world, int player, int planet);
int travelTime(const World & world, int player, int from, int to, const Ships & ships);
}
