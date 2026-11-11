#include "gd/ai.hpp"
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <chrono>
using namespace gd;
void check(bool b, const char *m)
{
    if (!b)
        throw std::runtime_error(m);
}
template <class F> void rejects(F f)
{
    bool thrown = false;
    try
    {
        f();
    }
    catch (const std::exception &)
    {
        thrown = true;
    }
    check(thrown, "Invalid input accepted");
}
int main()
{
    try
    {
        GameFacade g;
        auto initial = encodeWorld(g.world());
        check(encodeWorld(decodeWorld(initial)) == initial, "snapshot roundtrip");
        rejects([&] { decodeWorld(initial.substr(0, initial.size() / 2)); });
        rejects([&] { decodeWorld(initial + "garbage"); });
        rejects([&] { decodeOrders("GDORDERS 1 2 0", 1); });
        rejects([&] { decodeOrders("GDORDERS 1 1 1 2 0 1 0 -1 0 0 0", 1); });
        check(!g.queue(1, {OrderKind::Build, 0, 0, 0}).empty(), "ownership");
        check(!g.queue(0, {OrderKind::Move, 0, 19, 0, {1, 0, 0, 0}}).empty(), "hyperlane validation");
        check(g.queue(0, {OrderKind::Build, 0, 0, 0}).empty(), "valid build");
        check(g.world().players[0].resources.metal == 140, "planning isolation");
        check(g.preview(0).players[0].resources.metal == 105, "resource reservation");
        g.clear(0);
        check(g.queue(0, {OrderKind::Research, 0, 0, 0}).empty(), "research order");
        g.commit(0);
        check(!g.resolve(), "both players must be ready");
        g.commit(1);
        check(g.resolve(), "turn resolution");
        check(g.world().players[0].tech[0], "science spending");
        check(g.undo() && encodeWorld(g.world()) == initial, "memento exact restoration");
        auto w = g.world();
        w.planets[0].ships = {100, 0, 0, 0};
        w.planets[1].defense = 0;
        g.replace(w);
        g.queue(0, {OrderKind::Move, 0, 1, 0, {100, 0, 0, 0}});
        for (int turn = 0; turn < 3; ++turn)
        {
            g.commit(0);
            g.commit(1);
            g.resolve();
        }
        check(g.world().planets[1].owner == 0, "neutral conquest");
        w = makeWorld();
        w.planets[0].owner = 1;
        w.planets[19].owner = 0;
        g.replace(w);
        g.commit(0);
        g.commit(1);
        g.resolve();
        check(g.world().winner == 2, "simultaneous capital loss is draw");
        w = makeWorld();
        w.players[0].resources = {0, 0, 0};
        w.planets[0].ships = {200, 0, 0, 0};
        g.replace(w);
        g.commit(0);
        g.commit(1);
        g.resolve();
        check(g.world().players[0].shortage, "energy shortage");
        for (int style = 0; style < 4; ++style)
        {
            GameFacade mirrored(77);
            auto ai = makeAI(style);
            for (int turn = 0; turn < 30 && mirrored.world().winner == -1; ++turn)
            {
                for (int p = 0; p < 2; ++p)
                {
                    for (auto order : ai->plan(mirrored.world(), p))
                        check(mirrored.queue(p, order).empty(), "Mirror AI orders");
                    mirrored.commit(p);
                }
                mirrored.resolve();
                const auto &state = mirrored.world();
                check(state.players[0].resources == state.players[1].resources,
                      "No side advantage in resource allocation");
                for (int id = 0; id < 20; ++id)
                {
                    const auto &a = state.planets[id];
                    const auto &b = state.planets[19 - id];
                    check(a.ships == b.ships && a.defense == b.defense, "Mirrored combat and fleet strength");
                }
            }
        }
        int completed = 0;
        for (int style = 0; style < 4; ++style)
        {
            GameFacade match(100 + style);
            auto a = makeAI(style), b = makeAI((style + 1) % 4);
            for (int turn = 0; turn < 180 && match.world().winner == -1; ++turn)
            {
                for (int p = 0; p < 2; ++p)
                {
                    auto orders = (p == 0 ? a : b)->plan(match.world(), p);
                    for (auto o : orders)
                        check(match.queue(p, o).empty(), "AI issued invalid order");
                    match.commit(p);
                }
                check(match.resolve(), "AI turn");
                check(encodeWorld(decodeWorld(encodeWorld(match.world()))) == encodeWorld(match.world()),
                      "ongoing save roundtrip");
            }
            int colonies = 0;
            for (auto &p : match.world().planets)
                if (p.owner >= 0)
                    ++colonies;
            check(colonies > 2, "AI must expand");
            if (match.world().winner >= 0)
                ++completed;
            std::cout << "AI " << style << ": turn " << match.world().turn << ", winner "
                      << match.world().winner << ", colonies " << colonies << '\n';
        }
        check(completed >= 2, "AI games must reach victory");
        auto path = (std::filesystem::temp_directory_path() /
                     ("gd-test-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".save"))
                        .string();
        saveWorld(g.world(), path);
        check(encodeWorld(loadWorld(path)) == encodeWorld(g.world()), "disk save");
        std::filesystem::remove(path);
        std::cout << "Gameplay, AI, save and validation checks passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
