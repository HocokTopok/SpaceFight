#pragma once
#include "gd/game.hpp"
namespace gd
{
///////////////////////////////////////////////////////////////////////////////
// Pattern Strategy
///////////////////////////////////////////////////////////////////////////////
class AIStrategy
{
public :
    virtual ~AIStrategy() = default;
    virtual std::vector<Order> plan(const World &world, int player) const = 0;
    virtual std::string name() const = 0;
};
class AggressiveStrategy : public AIStrategy
{
public :
    std::vector<Order> plan(const World &world, int player) const override;
    std::string name() const override
    {
        return "Aggressive";
    }
};
class EconomicStrategy : public AIStrategy
{
public :
    std::vector<Order> plan(const World &world, int player) const override;
    std::string name() const override
    {
        return "Economic";
    }
};
class DefensiveStrategy : public AIStrategy
{
public :
    std::vector<Order> plan(const World &world, int player) const override;
    std::string name() const override
    {
        return "Defensive";
    }
};
class ScientificStrategy : public AIStrategy
{
public :
    std::vector<Order> plan(const World &world, int player) const override;
    std::string name() const override
    {
        return "Scientific";
    }
};
///////////////////////////////////////////////////////////////////////////////
std::unique_ptr<AIStrategy> makeAI(int style);
} // namespace gd
