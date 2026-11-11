#pragma once
#include "gd/model.hpp"
#include <functional>
#include <optional>

namespace gd
{
enum class OrderKind { Build, Produce, Move, Research };
struct Order
{
    OrderKind kind = OrderKind::Build;
    int source = 0, target = 0, item = 0;
    Ships ships{};
};
struct Event { std::string text; int planet = -1; bool battle = false; };
///////////////////////////////////////////////////////////////////////////////
// Pattern Observer
///////////////////////////////////////////////////////////////////////////////
class EventObserver
{
public :
    virtual ~EventObserver() = default;
    virtual void on_event(const Event & event) = 0;
};
class EventBus
{
public :
    void subscribe(std::shared_ptr<EventObserver> observer);
    void publish(const Event & event);
private :
    std::vector<std::weak_ptr<EventObserver>> m_observers;
};
class EventLog : public EventObserver
{
public :
    void on_event(const Event & event) override;
    std::vector<Event> entries;
};
///////////////////////////////////////////////////////////////////////////////
// Pattern Command
///////////////////////////////////////////////////////////////////////////////
class OrderCommand
{
public :
    virtual ~OrderCommand() = default;
    virtual std::string execute(World & world, int player) const = 0;
};
class BuildCommand : public OrderCommand
{
public :
    explicit BuildCommand(Order order) : m_order(order) {}
    std::string execute(World & world, int player) const override;
private :
    Order m_order;
};
class ProduceCommand : public OrderCommand
{
public :
    explicit ProduceCommand(Order order) : m_order(order) {}
    std::string execute(World & world, int player) const override;
private :
    Order m_order;
};
class MoveCommand : public OrderCommand
{
public :
    explicit MoveCommand(Order order) : m_order(order) {}
    std::string execute(World & world, int player) const override;
private :
    Order m_order;
};
class ResearchCommand : public OrderCommand
{
public :
    explicit ResearchCommand(Order order) : m_order(order) {}
    std::string execute(World & world, int player) const override;
private :
    Order m_order;
};
std::unique_ptr<OrderCommand> makeCommand(Order order);
std::string applyOrder(World & world, int player, Order order);
///////////////////////////////////////////////////////////////////////////////
// Pattern Template Method
///////////////////////////////////////////////////////////////////////////////
class TurnProcessor
{
public :
    virtual ~TurnProcessor() = default;
    void run(World & world, EventBus & events) const;
private :
    virtual void move_and_fight(World & world, EventBus & events) const = 0;
    virtual void produce(World & world, EventBus & events) const = 0;
    virtual void research(World & world, EventBus & events) const = 0;
    void victory(World & world, EventBus & events) const;
};
class StandardTurn : public TurnProcessor
{
private :
    void move_and_fight(World & world, EventBus & events) const override;
    void produce(World & world, EventBus & events) const override;
    void research(World & world, EventBus & events) const override;
};
///////////////////////////////////////////////////////////////////////////////
// Pattern Facade
///////////////////////////////////////////////////////////////////////////////
class GameFacade
{
public :
    explicit GameFacade(std::uint32_t seed = 42);
    const World & world() const { return m_world; }
    World preview(int player) const;
    std::string queue(int player, Order order);
    void clear(int player);
    bool ready(int player) const { return m_ready.at(player); }
    void commit(int player);
    bool resolve();
    bool undo();
    void replace(World world);
    const std::vector<Order> & orders(int player) const { return m_orders.at(player); }
    EventBus & events() { return m_events; }
    std::shared_ptr<EventLog> log() { return m_log; }
private :
///////////////////////////////////////////////////////////////////////////////
// Pattern Memento
///////////////////////////////////////////////////////////////////////////////
    struct Memento { World world; };
    std::optional<Memento> m_before;
///////////////////////////////////////////////////////////////////////////////
    World m_world;
    std::array<std::vector<Order>, 2> m_orders;
    std::array<bool, 2> m_ready{};
    EventBus m_events;
    std::shared_ptr<EventLog> m_log;
    StandardTurn m_turn;
};
///////////////////////////////////////////////////////////////////////////////
std::string encodeWorld(const World & world);
World decodeWorld(const std::string & text);
std::string encodeOrders(int turn, const std::vector<Order> & orders);
std::vector<Order> decodeOrders(const std::string & text, int expectedTurn);
void saveWorld(const World & world, const std::string & path);
World loadWorld(const std::string & path);
}
