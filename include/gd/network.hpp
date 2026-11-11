#pragma once
#include "gd/game.hpp"
#include <SFML/Network.hpp>
#include <SFML/System/Clock.hpp>
#include <deque>

namespace gd
{
class NetworkSession
{
public:
    explicit NetworkSession(GameFacade & game) : m_game(game) {}
    void host(unsigned short port = 4242);
    void join(const std::string & ip, unsigned short port = 4242);
    void update();
    void submit();
    bool connected() const { return m_connected && m_handshake; }
    bool hosting() const { return m_host; }
    int player() const { return m_host ? 0 : 1; }
    unsigned short port() const { return m_listener.getLocalPort(); }
    const std::string & status() const { return m_status; }
    bool failed() const { return m_failed; }
private:
    void send(std::string text);
    void process(const std::string & text);
    void fail(const std::string & message);
    GameFacade & m_game;
    sf::TcpListener m_listener;
    sf::TcpSocket m_socket;
    bool m_host = false, m_listening = false, m_connected = false, m_handshake = false, m_failed = false;
    std::string m_status, m_input;
    std::deque<std::string> m_output;
    std::size_t m_sent = 0;
    sf::Clock m_activity, m_heartbeat;
};
}
