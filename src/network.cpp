#include "gd/network.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <sstream>
#include <iomanip>

namespace gd
{
void NetworkSession::host(unsigned short port)
{
    m_host = true;
    if (m_listener.listen(port) != sf::Socket::Status::Done)
        throw std::runtime_error("Cannot listen on this port");
    m_listener.setBlocking(false);
    m_listening = true;
    m_status = "Waiting for player on port " + std::to_string(m_listener.getLocalPort());
}
void NetworkSession::join(const std::string &ip, unsigned short port)
{
    if (ip.empty() || ip.size() > 15 ||
        !std::ranges::all_of(ip, [](unsigned char c) { return std::isdigit(c) || c == '.'; }))
        throw std::runtime_error("Enter an IPv4 address");
    auto address = sf::IpAddress::resolve(ip);
    if (!address || m_socket.connect(*address, port, sf::seconds(2)) != sf::Socket::Status::Done)
        throw std::runtime_error("Connection failed; check IP and port 4242");
    m_socket.setBlocking(false);
    m_connected = true;
    m_activity.restart();
    send("HELLO 1");
    m_status = "Connected; awaiting galaxy";
}
void NetworkSession::fail(const std::string &message)
{
    m_status = message;
    m_failed = true;
    m_connected = m_listening = m_handshake = false;
    m_socket.disconnect();
    m_listener.close();
    m_output.clear();
    m_input.clear();
}
void NetworkSession::send(std::string text)
{
    if (text.size() > 1024 * 1024 || m_output.size() > 16)
        throw std::runtime_error("Network queue limit");
    std::uint32_t n = static_cast<std::uint32_t>(text.size());
    std::string frame(4, '\0');
    for (int i = 0; i < 4; ++i)
        frame[i] = static_cast<char>((n >> (24 - i * 8)) & 255);
    frame += text;
    m_output.push_back(std::move(frame));
}
void NetworkSession::process(const std::string &text)
{
    if (text == "PING")
        return;
    if (m_host && !m_handshake && text == "HELLO 1")
    {
        m_handshake = true;
        m_listener.close();
        send("WORLD " + encodeWorld(m_game.world()));
        m_status = "Player joined / planning";
        return;
    }
    if (!m_host && text.starts_with("WORLD "))
    {
        auto world = decodeWorld(text.substr(6));
        if (m_handshake && world.turn != m_game.world().turn + 1)
            throw std::runtime_error("Unexpected turn snapshot");
        m_game.replace(std::move(world));
        m_handshake = true;
        m_status = "Connected / planning";
        return;
    }
    if (!m_handshake)
        throw std::runtime_error("Protocol handshake failed");
    if (m_host && text.starts_with("ORDERS "))
    {
        if (m_game.ready(1))
            throw std::runtime_error("Duplicate turn submission");
        auto orders = decodeOrders(text.substr(7), m_game.world().turn);
        auto preview = m_game.world();
        for (auto o : orders)
            if (auto error = applyOrder(preview, 1, o); !error.empty())
                throw std::runtime_error("Invalid remote order: " + error);
        m_game.clear(1);
        for (auto o : orders)
            if (!m_game.queue(1, o).empty())
                throw std::runtime_error("Remote order rejected");
        m_game.commit(1);
        m_status = "Opponent ready";
        return;
    }
    if (!m_host && text.starts_with("EVENTS "))
    {
        std::istringstream in(text.substr(7));
        int count;
        if (!(in >> count) || count < 0 || count > 100)
            throw std::runtime_error("Invalid event count");
        for (int i = 0; i < count; ++i)
        {
            Event event;
            int battle;
            if (!(in >> event.planet >> battle >> std::quoted(event.text)) || event.planet < -1 ||
                event.planet >= static_cast<int>(m_game.world().planets.size()) || battle < 0 || battle > 1 ||
                event.text.size() > 200)
                throw std::runtime_error("Invalid event");
            event.battle = battle;
            m_game.events().publish(event);
        }
        in >> std::ws;
        if (!in.eof())
            throw std::runtime_error("Trailing event data");
        return;
    }
    if (!m_host && text == "READY")
    {
        m_status = "Host ready";
        return;
    }
    throw std::runtime_error("Unexpected network message");
}
void NetworkSession::submit()
{
    if (!connected() || m_game.ready(player()) || m_game.world().winner != -1)
        return;
    if (m_host)
        send("READY");
    else
        send("ORDERS " + encodeOrders(m_game.world().turn, m_game.orders(1)));
    m_game.commit(player());
    m_status = "Orders locked / waiting for opponent";
}
void NetworkSession::update()
{
    if (m_failed)
        return;
    try
    {
        if (m_listening && !m_connected)
        {
            auto status = m_listener.accept(m_socket);
            if (status == sf::Socket::Status::Done)
            {
                m_connected = true;
                m_socket.setBlocking(false);
                m_activity.restart();
                m_status = "Player connecting";
            }
            else if (status == sf::Socket::Status::Error)
                throw std::runtime_error("Accept failed");
        }
        if (!m_connected)
            return;
        if (m_activity.getElapsedTime().asSeconds() > (m_handshake ? 30.f : 10.f))
            throw std::runtime_error("Peer timed out; return to menu to reconnect");
        if (m_handshake && m_heartbeat.getElapsedTime().asSeconds() > 5.f)
        {
            send("PING");
            m_heartbeat.restart();
        }
        for (int batch = 0; batch < 16; ++batch)
        {
            char bytes[8192];
            std::size_t received = 0;
            auto status = m_socket.receive(bytes, sizeof(bytes), received);
            if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error)
                throw std::runtime_error("Peer disconnected; match paused");
            if (status == sf::Socket::Status::NotReady)
                break;
            m_activity.restart();
            m_input.append(bytes, received);
            if (m_input.size() > 1024 * 1024 + 4)
                throw std::runtime_error("Incoming frame too large");
            while (m_input.size() >= 4)
            {
                std::uint32_t length = 0;
                for (int i = 0; i < 4; ++i)
                    length = (length << 8) | static_cast<unsigned char>(m_input[i]);
                if (length == 0 || length > 1024 * 1024)
                    throw std::runtime_error("Invalid frame size");
                if (m_input.size() < length + 4)
                    break;
                auto message = m_input.substr(4, length);
                m_input.erase(0, length + 4);
                process(message);
            }
        }
        if (m_host && m_handshake && m_game.ready(0) && m_game.ready(1) && m_game.world().winner == -1)
        {
            m_game.log()->entries.clear();
            m_game.resolve();
            send("WORLD " + encodeWorld(m_game.world()));
            std::ostringstream events;
            const auto &entries = m_game.log()->entries;
            events << "EVENTS " << entries.size() << ' ';
            for (const auto &event : entries)
                events << event.planet << ' ' << event.battle << ' ' << std::quoted(event.text) << ' ';
            send(events.str());
            m_status = "Connected / planning";
        }
        while (!m_output.empty())
        {
            auto &frame = m_output.front();
            std::size_t sent = 0;
            auto status = m_socket.send(frame.data() + m_sent, frame.size() - m_sent, sent);
            m_sent += sent;
            if (status == sf::Socket::Status::Disconnected || status == sf::Socket::Status::Error)
                throw std::runtime_error("Peer disconnected while sending");
            if (m_sent == frame.size())
            {
                m_output.pop_front();
                m_sent = 0;
            }
            else
                break;
        }
    }
    catch (const std::exception &e)
    {
        fail(e.what());
    }
}
} // namespace gd
