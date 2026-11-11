#include "gd/game.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace gd
{
void EventBus::subscribe(std::shared_ptr<EventObserver> observer) { m_observers.push_back(observer); }
void EventBus::publish(const Event & e)
{
    std::erase_if(m_observers, [](const auto & p) { return p.expired(); });
    for (auto & p : m_observers) if (auto observer = p.lock()) observer->on_event(e);
}
void EventLog::on_event(const Event & e)
{
    entries.push_back(e); if (entries.size() > 100) entries.erase(entries.begin());
}
static bool validPlanet(const World & w, int id) { return id >= 0 && id < static_cast<int>(w.planets.size()); }
static std::string ownPlanet(const World & w, int player, int id)
{
    if (!validPlanet(w, id)) return "Invalid planet";
    if (w.planets[id].owner != player) return "Select one of your colonies";
    return {};
}
std::string BuildCommand::execute(World & w, int player) const
{
    if (auto e = ownPlanet(w, player, m_order.source); !e.empty()) return e;
    if (m_order.item < 0 || m_order.item >= 6) return "Unknown building";
    auto & p = w.planets[m_order.source]; auto & empire = w.players[player];
    int used = std::accumulate(p.buildings.begin(), p.buildings.end(), 0);
    for (auto j : p.jobs) if (!j.ship) ++used;
    if (used >= p.slots) return "All building slots reserved";
    const auto & spec = buildingSpecs()[m_order.item];
    auto cost = price(spec.cost, empire);
    if (!empire.resources.covers(cost)) return "Not enough metal or energy";
    if (p.jobs.size() >= 12) return "Production queue full";
    empire.resources -= cost;
    p.jobs.push_back({false, m_order.item, std::max(1, spec.turns - int(empire.tech[1]))});
    return {};
}
std::string ProduceCommand::execute(World & w, int player) const
{
    if (auto e = ownPlanet(w, player, m_order.source); !e.empty()) return e;
    if (m_order.item < 0 || m_order.item >= 4) return "Unknown ship";
    auto & p = w.planets[m_order.source]; auto & empire = w.players[player];
    if (!p.buildings[3]) return "Build a shipyard first";
    if (m_order.item >= 2 && !empire.tech[5]) return "Research Heavy ships first";
    if (p.jobs.size() >= 12) return "Production queue full";
    if (shipCount(p.ships) + static_cast<int>(p.jobs.size()) >= 3000) return "Orbit capacity reached";
    const auto & spec = shipSpecs()[m_order.item]; auto cost = price(spec.cost, empire);
    if (!empire.resources.covers(cost)) return "Not enough metal or energy";
    empire.resources -= cost;
    p.jobs.push_back({true, m_order.item, std::max(1, spec.turns - int(empire.tech[1]))});
    return {};
}
std::string MoveCommand::execute(World & w, int player) const
{
    if (auto e = ownPlanet(w, player, m_order.source); !e.empty()) return e;
    if (!validPlanet(w, m_order.target) || !linked(w, m_order.source, m_order.target)) return "Destination must share a hyperlane";
    auto & p = w.planets[m_order.source];
    for (int t = 0; t < 4; ++t) if (m_order.ships[t] < 0 || m_order.ships[t] > p.ships[t]) return "Not enough ships in orbit";
    if (!shipCount(m_order.ships)) return "No ships selected";
    if (w.transits.size() >= 512) return "Too many fleets in transit";
    int duration = travelTime(w, player, m_order.source, m_order.target, m_order.ships);
    for (int t = 0; t < 4; ++t) p.ships[t] -= m_order.ships[t];
    w.transits.push_back({player, m_order.source, m_order.target, duration, duration, m_order.ships});
    return {};
}
std::string ResearchCommand::execute(World & w, int player) const
{
    int id = m_order.item;
    if (id < 0 || id >= 9) return "Unknown technology";
    auto & p = w.players[player]; const auto & spec = techSpecs()[id];
    if (p.research != -1) return "Research already in progress";
    if (p.tech[id]) return "Technology already researched";
    if (spec.prerequisite >= 0 && !p.tech[spec.prerequisite]) return "Research prerequisite first";
    p.research = id; p.progress = 0; return {};
}
std::unique_ptr<OrderCommand> makeCommand(Order o)
{
    switch (o.kind)
    {
        case OrderKind::Build: return std::make_unique<BuildCommand>(o);
        case OrderKind::Produce: return std::make_unique<ProduceCommand>(o);
        case OrderKind::Move: return std::make_unique<MoveCommand>(o);
        case OrderKind::Research: return std::make_unique<ResearchCommand>(o);
    }
    throw std::invalid_argument("Unknown order kind");
}
std::string applyOrder(World & w, int player, Order o)
{
    if (player < 0 || player > 1) return "Invalid player";
    if (w.winner != -1) return "Game has ended";
    if (static_cast<int>(o.kind) < 0 || static_cast<int>(o.kind) > 3) return "Unknown order kind";
    return makeCommand(o)->execute(w, player);
}
void TurnProcessor::run(World & w, EventBus & events) const
{
    if (w.winner != -1) return;
    move_and_fight(w, events);
    produce(w, events);
    research(w, events);
    victory(w, events);
    ++w.turn;
}
void TurnProcessor::victory(World & w, EventBus & events) const
{
    std::array<bool, 2> alive{}, capitalLost{};
    for (auto & p : w.planets)
    {
        if (p.owner >= 0) alive[p.owner] = true;
        if (p.capital >= 0 && p.owner != p.capital) capitalLost[p.capital] = true;
    }
    bool lost0 = !alive[0] || capitalLost[0], lost1 = !alive[1] || capitalLost[1];
    if (lost0 || lost1)
    {
        w.winner = lost0 && lost1 ? 2 : (lost0 ? 1 : 0);
        events.publish({w.winner == 2 ? "Mutual capital loss: draw" : (w.winner == 0 ? "AURORA wins the galaxy" : "EMBER wins the galaxy")});
    }
}
namespace
{
struct BattleSide
{
    std::array<double, 4> hp{};
    std::array<FleetStats, 4> unit{};
    double guard = 0;
    bool shortage = false;
    double health() const { return std::accumulate(hp.begin(), hp.end(), guard); }
    Ships ships() const
    {
        Ships s{}; for (int t = 0; t < 4; ++t) if (hp[t] > 0.001) s[t] = static_cast<int>(std::ceil(hp[t] / unit[t].armor));
        return s;
    }
    double attack() const
    {
        auto s = ships(); double a = guard * 0.45;
        for (int t = 0; t < 4; ++t) a += s[t] * unit[t].attack;
        return a * (shortage ? 0.65 : 1.0);
    }
    double siege() const { return ships()[3] * unit[3].siege; }
    void hit(double damage, double siegeDamage)
    {
        double h = health(); if (h < 0.001) return;
        double ratio = std::max(0.0, 1.0 - damage / h);
        for (auto & value : hp) value *= ratio;
        guard = std::max(0.0, guard * ratio - siegeDamage);
        if (health() < 0.05) { hp = {}; guard = 0; }
    }
};
void battle(World & w, Planet & p, std::array<Ships, 2> forces, EventBus & events)
{
    std::array<BattleSide, 3> side;
    for (int owner = 0; owner < 2; ++owner)
    {
        for (int t = 0; t < 4; ++t)
        {
            Ships one{}; one[t] = 1;
            side[owner].unit[t] = makeFleet(one, w.players[owner].tech)->stats();
            side[owner].hp[t] = forces[owner][t] * side[owner].unit[t].armor;
        }
        side[owner].shortage = w.players[owner].shortage;
    }
    side[p.owner == -1 ? 2 : p.owner].guard = p.defense;
    int active = 0; for (auto & s : side) if (s.health() > 0) ++active;
    bool fought = active > 1;
    for (int round = 0; round < 128 && active > 1; ++round)
    {
        std::array<double, 3> damage{}, siege{};
        for (int a = 0; a < 3; ++a) if (side[a].health() > 0)
            for (int b = 0; b < 3; ++b) if (a != b && side[b].health() > 0)
            { damage[b] += side[a].attack() / (active - 1); siege[b] += side[a].siege() / (active - 1); }
        active = 0;
        for (int i = 0; i < 3; ++i) { side[i].hit(damage[i], siege[i]); if (side[i].health() > 0) ++active; }
    }
    p.ships = {};
    int survivor = -1;
    for (int i = 0; i < 3; ++i) if (side[i].health() > 0) survivor = i;
    if (survivor >= 0 && survivor < 2)
    {
        p.ships = side[survivor].ships();
        if (p.owner != survivor && shipCount(p.ships))
        {
            p.owner = survivor; p.jobs.clear();
            events.publish({p.name + " captured by " + (survivor == 0 ? "AURORA" : "EMBER"), p.id, true});
        }
    }
    p.defense = survivor < 0 ? 0 : static_cast<int>(side[survivor].guard);
    if (fought) events.publish({"Battle at " + p.name + ": " + std::to_string(shipCount(p.ships)) + " ships remain", p.id, true});
}
}
void StandardTurn::move_and_fight(World & w, EventBus & events) const
{
    std::vector<std::array<Ships, 2>> arrivals(w.planets.size());
    for (auto & f : w.transits) if (--f.remaining <= 0)
        for (int t = 0; t < 4; ++t) arrivals[f.to][f.owner][t] += f.ships[t];
    std::erase_if(w.transits, [](const Transit & f) { return f.remaining <= 0; });
    for (auto & p : w.planets)
    {
        auto forces = arrivals[p.id];
        if (p.owner >= 0) for (int t = 0; t < 4; ++t) forces[p.owner][t] += p.ships[t];
        if (shipCount(forces[0]) || shipCount(forces[1])) battle(w, p, forces, events);
    }
}
void StandardTurn::produce(World & w, EventBus & events) const
{
    std::array<int, 2> upkeep{};
    for (auto & f : w.transits) upkeep[f.owner] += shipCount(f.ships);
    for (auto & p : w.planets) if (p.owner >= 0)
    {
        auto & player = w.players[p.owner];
        player.resources += income(p, player);
        upkeep[p.owner] += shipCount(p.ships);
        p.defense = std::min(10 + p.buildings[4] * 20, p.defense + 2 + p.buildings[4] * 3);
        for (auto & j : p.jobs) if (--j.remaining <= 0)
        {
            if (j.ship) ++p.ships[j.type]; else ++p.buildings[j.type];
            events.publish({p.name + ": " + (j.ship ? shipSpecs()[j.type].name : buildingSpecs()[j.type].name) + " ready", p.id});
        }
        std::erase_if(p.jobs, [](Job j) { return j.remaining <= 0; });
    }
    for (int i = 0; i < 2; ++i)
    {
        auto & p = w.players[i]; p.resources.energy -= (upkeep[i] + 2) / 3;
        p.shortage = p.resources.energy < 0;
        if (p.shortage) events.publish({std::string(i == 0 ? "AURORA" : "EMBER") + ": energy shortage, fleet attack -35%"});
        p.resources.energy = std::clamp(p.resources.energy, 0, 1000000);
        p.resources.metal = std::clamp(p.resources.metal, 0, 1000000);
        p.resources.science = std::clamp(p.resources.science, 0, 1000000);
    }
}
void StandardTurn::research(World & w, EventBus & events) const
{
    for (auto & p : w.players) if (p.research >= 0)
    {
        const auto & spec = techSpecs()[p.research];
        int spend = std::min(p.resources.science, spec.cost - p.progress);
        p.resources.science -= spend; p.progress += spend;
        if (p.progress >= spec.cost)
        {
            p.tech[p.research] = true;
            events.publish({"Research complete: " + spec.name});
            p.research = -1; p.progress = 0;
        }
    }
}
GameFacade::GameFacade(std::uint32_t seed) : m_world(makeWorld(seed)), m_log(std::make_shared<EventLog>()) { m_events.subscribe(m_log); }
World GameFacade::preview(int player) const
{
    auto copy = m_world; for (auto o : m_orders.at(player)) applyOrder(copy, player, o); return copy;
}
std::string GameFacade::queue(int player, Order o)
{
    if (player < 0 || player > 1) return "Invalid player";
    if (m_ready[player]) return "Orders already committed";
    if (m_orders[player].size() >= 64) return "Order limit reached";
    auto copy = preview(player); auto error = applyOrder(copy, player, o);
    if (error.empty()) m_orders[player].push_back(o);
    return error;
}
void GameFacade::clear(int player) { if (!m_ready.at(player)) m_orders.at(player).clear(); }
void GameFacade::commit(int player) { m_ready.at(player) = true; }
bool GameFacade::resolve()
{
    if (!m_ready[0] || !m_ready[1] || m_world.winner != -1) return false;
    m_before = Memento{m_world};
    for (int player = 0; player < 2; ++player) for (auto o : m_orders[player])
    {
        auto error = applyOrder(m_world, player, o);
        if (!error.empty()) m_events.publish({"Order rejected: " + error});
    }
    m_turn.run(m_world, m_events); m_orders = {}; m_ready = {}; return true;
}
bool GameFacade::undo()
{
    if (!m_before) return false;
    auto snapshot = std::move(m_before->world); replace(std::move(snapshot));
    m_events.publish({"Previous turn restored"}); return true;
}
void GameFacade::replace(World w)
{
    m_world = std::move(w); m_orders = {}; m_ready = {}; m_before.reset(); m_log->entries.clear();
}
}
