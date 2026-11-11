#include "gd/model.hpp"
#include <iostream>
#include <stdexcept>
#include <queue>
void check(bool value, const char * message) { if (!value) throw std::runtime_error(message); }
int main()
{
    try
    {
        for (unsigned seed = 0; seed < 100; ++seed)
        {
            auto w = gd::makeWorld(seed);
            check(w.planets.size() == 20, "20 planets");
            std::array<bool, 20> seen{}; std::queue<int> q; q.push(0); seen[0] = true;
            while (!q.empty()) { auto a = q.front(); q.pop(); for (int b = 0; b < 20; ++b) if (!seen[b] && gd::linked(w, a, b)) { seen[b] = true; q.push(b); } }
            for (bool b : seen) check(b, "connected galaxy");
            check(w.planets[0].ships == w.planets[19].ships, "symmetric starts");
        }
        gd::Technologies tech{};
        auto plain = gd::makeFleet({2, 1, 0, 0}, tech)->stats();
        check(plain.count == 3 && plain.attack == 34 && plain.armor == 59, "recursive fleet stats");
        tech[3] = tech[4] = tech[6] = true;
        auto improved = gd::makeFleet({2, 1, 0, 0}, tech)->stats();
        check(improved.attack > plain.attack && improved.armor > plain.armor && improved.speed > plain.speed, "stacked decorators");
        std::cout << "Core model checks passed\n";
    }
    catch (const std::exception & e) { std::cerr << e.what() << '\n'; return 1; }
}
