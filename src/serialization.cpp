#include "gd/game.hpp"
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cmath>
#include <queue>
#include <set>
#include <stdexcept>

namespace gd
{
namespace
{
int number(std::istream &in, int low, int high)
{
    long long n;
    if (!(in >> n) || n < low || n > high)
        throw std::runtime_error("Invalid numeric field");
    return static_cast<int>(n);
}
float coordinate(std::istream &in, float high)
{
    float n;
    if (!(in >> n) || !std::isfinite(n) || n < 0 || n > high)
        throw std::runtime_error("Invalid coordinate");
    return n;
}
void end(std::istream &in)
{
    in >> std::ws;
    if (!in.eof())
        throw std::runtime_error("Trailing data");
}
void header(std::istream &in, const char *expected)
{
    std::string tag;
    int version;
    if (!(in >> tag >> version) || tag != expected || version != 1)
        throw std::runtime_error("Unsupported file or protocol version");
}
void resources(std::ostream &out, Resources r)
{
    out << r.metal << ' ' << r.energy << ' ' << r.science << ' ';
}
Resources resources(std::istream &in)
{
    return {number(in, 0, 1000000), number(in, 0, 1000000), number(in, 0, 1000000)};
}
void ships(std::ostream &out, const Ships &s)
{
    for (auto n : s)
        out << n << ' ';
}
Ships ships(std::istream &in)
{
    Ships s{};
    for (auto &n : s)
        n = number(in, 0, 6000);
    if (shipCount(s) > 6000)
        throw std::runtime_error("Fleet exceeds limit");
    return s;
}
} // namespace
std::string encodeWorld(const World &w)
{
    std::ostringstream out;
    out << std::setprecision(9) << "GDWORLD 1\n"
        << w.seed << ' ' << w.turn << ' ' << w.winner << ' ' << w.aiStyle << '\n';
    for (const auto &p : w.players)
    {
        resources(out, p.resources);
        for (bool t : p.tech)
            out << t << ' ';
        out << p.research << ' ' << p.progress << ' ' << p.shortage << '\n';
    }
    out << w.planets.size() << '\n';
    for (const auto &p : w.planets)
    {
        out << p.id << ' ' << std::quoted(p.name) << ' ' << int(p.type) << ' ' << p.x << ' ' << p.y << ' '
            << p.radius << ' ' << p.owner << ' ' << p.capital << ' ' << p.slots << ' ' << p.defense << ' ';
        resources(out, p.production);
        for (auto b : p.buildings)
            out << b << ' ';
        ships(out, p.ships);
        out << p.jobs.size() << ' ';
        for (auto j : p.jobs)
            out << j.ship << ' ' << j.type << ' ' << j.remaining << ' ';
        out << '\n';
    }
    out << w.lanes.size() << '\n';
    for (auto e : w.lanes)
        out << e[0] << ' ' << e[1] << '\n';
    out << w.transits.size() << '\n';
    for (const auto &f : w.transits)
    {
        out << f.owner << ' ' << f.from << ' ' << f.to << ' ' << f.remaining << ' ' << f.duration << ' ';
        ships(out, f.ships);
        out << '\n';
    }
    return out.str();
}
World decodeWorld(const std::string &text)
{
    if (text.size() > 1024 * 1024)
        throw std::runtime_error("Snapshot too large");
    std::istringstream in(text);
    header(in, "GDWORLD");
    World w;
    unsigned long long seed;
    if (!(in >> seed) || seed > UINT32_MAX)
        throw std::runtime_error("Invalid seed");
    w.seed = static_cast<std::uint32_t>(seed);
    w.turn = number(in, 1, 1000000);
    w.winner = number(in, -1, 2);
    w.aiStyle = number(in, 0, 3);
    for (auto &p : w.players)
    {
        p.resources = resources(in);
        for (auto &t : p.tech)
            t = number(in, 0, 1);
        p.research = number(in, -1, 8);
        p.progress = number(in, 0, 85);
        p.shortage = number(in, 0, 1);
        for (int t = 0; t < 9; ++t)
            if (p.tech[t] && techSpecs()[t].prerequisite >= 0 && !p.tech[techSpecs()[t].prerequisite])
                throw std::runtime_error("Invalid technology tree");
        if (p.research >= 0 &&
            (p.tech[p.research] || p.progress >= techSpecs()[p.research].cost ||
             (techSpecs()[p.research].prerequisite >= 0 && !p.tech[techSpecs()[p.research].prerequisite])))
            throw std::runtime_error("Invalid research");
        if (p.research == -1 && p.progress != 0)
            throw std::runtime_error("Orphan research progress");
    }
    int count = number(in, 15, 25);
    std::array<int, 2> capitals{};
    for (int i = 0; i < count; ++i)
    {
        Planet p;
        p.id = number(in, i, i);
        if (!(in >> std::quoted(p.name)) || p.name.empty() || p.name.size() > 40 ||
            p.name.find_first_of("\r\n\t") != std::string::npos)
            throw std::runtime_error("Invalid planet name");
        p.type = static_cast<PlanetType>(number(in, 0, 4));
        p.x = coordinate(in, 1000);
        p.y = coordinate(in, 1000);
        p.radius = coordinate(in, 60);
        p.owner = number(in, -1, 1);
        p.capital = number(in, -1, 1);
        p.slots = number(in, 1, 12);
        p.defense = number(in, 0, 300);
        if (p.capital >= 0)
            ++capitals[p.capital];
        p.production = resources(in);
        int used = 0;
        for (auto &b : p.buildings)
        {
            b = number(in, 0, 12);
            used += b;
        }
        p.ships = ships(in);
        int jobs = number(in, 0, 12);
        for (int j = 0; j < jobs; ++j)
        {
            Job job;
            job.ship = number(in, 0, 1);
            job.type = number(in, 0, job.ship ? 3 : 5);
            job.remaining = number(in, 1, 3);
            if (!job.ship)
                ++used;
            p.jobs.push_back(job);
        }
        if (used > p.slots || (p.owner == -1 && (shipCount(p.ships) || !p.jobs.empty())))
            throw std::runtime_error("Invalid colony");
        w.planets.push_back(p);
    }
    if (capitals != std::array<int, 2>{1, 1})
        throw std::runtime_error("Missing or duplicate capitals");
    int lanes = number(in, count - 1, 100);
    std::set<std::pair<int, int>> unique;
    for (int i = 0; i < lanes; ++i)
    {
        int a = number(in, 0, count - 1), b = number(in, 0, count - 1);
        if (a == b || !unique.emplace(std::min(a, b), std::max(a, b)).second)
            throw std::runtime_error("Invalid lane");
        w.lanes.push_back({a, b});
    }
    std::vector<bool> seen(count);
    std::queue<int> q;
    seen[0] = true;
    q.push(0);
    int reached = 0;
    while (!q.empty())
    {
        int a = q.front();
        q.pop();
        ++reached;
        for (int b = 0; b < count; ++b)
            if (!seen[b] && linked(w, a, b))
            {
                seen[b] = true;
                q.push(b);
            }
    }
    if (reached != count)
        throw std::runtime_error("Disconnected galaxy");
    int fleets = number(in, 0, 512);
    for (int i = 0; i < fleets; ++i)
    {
        Transit f;
        f.owner = number(in, 0, 1);
        f.from = number(in, 0, count - 1);
        f.to = number(in, 0, count - 1);
        f.remaining = number(in, 1, 30);
        f.duration = number(in, f.remaining, 30);
        f.ships = ships(in);
        if (!shipCount(f.ships) || !linked(w, f.from, f.to))
            throw std::runtime_error("Invalid transit");
        w.transits.push_back(f);
    }
    int totalShips = 0;
    for (const auto &p : w.planets)
    {
        totalShips += shipCount(p.ships);
        for (auto j : p.jobs)
            if (j.ship)
                ++totalShips;
    }
    for (const auto &f : w.transits)
        totalShips += shipCount(f.ships);
    if (totalShips > 6000)
        throw std::runtime_error("Galaxy fleet capacity exceeded");
    end(in);
    return w;
}
std::string encodeOrders(int turn, const std::vector<Order> &orders)
{
    std::ostringstream out;
    out << "GDORDERS 1 " << turn << ' ' << orders.size() << '\n';
    for (auto o : orders)
    {
        out << int(o.kind) << ' ' << o.source << ' ' << o.target << ' ' << o.item << ' ';
        ships(out, o.ships);
        out << '\n';
    }
    return out.str();
}
std::vector<Order> decodeOrders(const std::string &text, int turn)
{
    if (text.size() > 32768)
        throw std::runtime_error("Orders too large");
    std::istringstream in(text);
    header(in, "GDORDERS");
    number(in, turn, turn);
    int count = number(in, 0, 64);
    std::vector<Order> result;
    for (int i = 0; i < count; ++i)
    {
        Order o;
        o.kind = static_cast<OrderKind>(number(in, 0, 3));
        o.source = number(in, 0, 24);
        o.target = number(in, 0, 24);
        o.item = number(in, 0, 8);
        o.ships = ships(in);
        result.push_back(o);
    }
    end(in);
    return result;
}
void saveWorld(const World &w, const std::string &path)
{
    auto tmp = path + ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary);
        file << encodeWorld(w);
        file.flush();
        if (!file)
            throw std::runtime_error("Could not write save");
    }
    std::filesystem::rename(tmp, path);
}
World loadWorld(const std::string &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() > 1024 * 1024)
        throw std::runtime_error("Save missing or too large");
    auto size = file.tellg();
    file.seekg(0);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!file.read(text.data(), size))
        throw std::runtime_error("Could not read save");
    return decodeWorld(text);
}
} // namespace gd
