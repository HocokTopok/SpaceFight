#include "gd/ai.hpp"
#include <algorithm>
#include <queue>

namespace gd
{
static std::vector<Order> planOrders(const World & original, int player, int style)
{
    auto w = original; std::vector<Order> result;
    auto add = [&](Order o) { if (result.size() < 64 && applyOrder(w, player, o).empty()) { result.push_back(o); return true; } return false; };
    const std::array<std::array<int, 9>, 4> priorities{{
        {3, 4, 5, 6, 0, 1, 2, 7, 8}, {0, 1, 2, 6, 3, 4, 5, 7, 8},
        {4, 3, 0, 6, 1, 5, 2, 7, 8}, {6, 7, 8, 0, 1, 3, 4, 5, 2}
    }};
    if (w.players[player].research == -1)
        for (int t : priorities[style]) if (add({OrderKind::Research, 0, 0, t})) break;
    for (int id = 0; id < static_cast<int>(w.planets.size()); ++id)
    {
        if (w.planets[id].owner != player) continue;
        auto p = w.planets[id];
        bool front = false;
        for (auto & q : w.planets) if (linked(w, id, q.id) && q.owner != player) front = true;
        auto planned = p.buildings;
        for (auto j : p.jobs) if (!j.ship) ++planned[j.type];
        if (w.players[player].resources.energy < 45 && planned[1] < 2) add({OrderKind::Build, id, 0, 1});
        else if (!planned[3] && front) add({OrderKind::Build, id, 0, 3});
        else if (style == 2 && front && !planned[4]) add({OrderKind::Build, id, 0, 4});
        else if (style == 3 && planned[2] < 2) add({OrderKind::Build, id, 0, 2});
        else if (planned[0] < (style == 1 ? 2 : 1)) add({OrderKind::Build, id, 0, 0});
        else if (!planned[2]) add({OrderKind::Build, id, 0, 2});
        else if (front && !planned[5] && w.turn > 8) add({OrderKind::Build, id, 0, 5});
        if (p.buildings[3])
        {
            int budget = style == 0 ? 4 : 3;
            for (int i = 0; i < budget; ++i)
            {
                int type = w.players[player].tech[5] && w.players[player].resources.metal > 150 ? (i == 0 ? 2 : 3) : (i == 0 ? 1 : 0);
                add({OrderKind::Produce, id, 0, type});
            }
        }
    }
    for (const auto & p : original.planets) if (p.owner == player && shipCount(p.ships) > 1)
    {
        std::vector<int> first(w.planets.size(), -1), dist(w.planets.size(), 999);
        std::queue<int> q; q.push(p.id); dist[p.id] = 0;
        while (!q.empty())
        {
            int a = q.front(); q.pop();
            for (auto & b : w.planets) if (linked(w, a, b.id) && dist[b.id] == 999)
            { dist[b.id] = dist[a] + 1; first[b.id] = a == p.id ? b.id : first[a]; if (b.owner == player) q.push(b.id); }
        }
        int target = -1; double best = -1e9;
        auto moving = p.ships;
        int reserve = style == 2 ? 2 : 1;
        moving[0] = std::max(0, moving[0] - reserve);
        auto power = makeFleet(moving, w.players[player].tech)->stats();
        for (auto & b : w.planets) if (b.owner != player && dist[b.id] < 999)
        {
            double threat = b.defense;
            if (b.owner >= 0) threat += visible(original, player, b.id) ? makeFleet(b.ships, w.players[b.owner].tech)->stats().armor : 120;
            if (dist[b.id] == 1 && power.armor < threat * (style == 0 ? 1.1 : 1.4)) continue;
            double score = 100.0 / dist[b.id] - threat * 0.08 + (b.owner == -1 ? 14 : (style == 0 ? 30 : 0));
            if (b.capital >= 0) score += 25;
            if (score > best) { best = score; target = first[b.id]; }
        }
        if (target >= 0 && power.count > 0) add({OrderKind::Move, p.id, target, 0, moving});
    }
    return result;
}
std::vector<Order> AggressiveStrategy::plan(const World & w, int p) const { return planOrders(w, p, 0); }
std::vector<Order> EconomicStrategy::plan(const World & w, int p) const { return planOrders(w, p, 1); }
std::vector<Order> DefensiveStrategy::plan(const World & w, int p) const { return planOrders(w, p, 2); }
std::vector<Order> ScientificStrategy::plan(const World & w, int p) const { return planOrders(w, p, 3); }
std::unique_ptr<AIStrategy> makeAI(int style)
{
    switch (style)
    {
        case 1: return std::make_unique<EconomicStrategy>();
        case 2: return std::make_unique<DefensiveStrategy>();
        case 3: return std::make_unique<ScientificStrategy>();
        default: return std::make_unique<AggressiveStrategy>();
    }
}
}
