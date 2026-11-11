#include "gd/network.hpp"
#include <iostream>
#include <thread>
#include <stdexcept>
using namespace gd;
void check(bool b, const char *m)
{
    if (!b)
        throw std::runtime_error(m);
}
int main()
{
    try
    {
        GameFacade hostGame(13), clientGame(99);
        NetworkSession host(hostGame), client(clientGame);
        host.host(0);
        client.join("127.0.0.1", host.port());
        auto pump = [&](auto condition)
        {
            for (int n = 0; n < 2000 && !condition(); ++n)
            {
                host.update();
                client.update();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            check(condition(), "Network timeout");
            check(!host.failed() && !client.failed(), "Network error");
        };
        pump([&] { return host.connected() && client.connected(); });
        check(encodeWorld(hostGame.world()) == encodeWorld(clientGame.world()), "Initial sync");
        bool sawEvents = false;
        for (int turn = 1; turn <= 5; ++turn)
        {
            if (turn == 1)
            {
                hostGame.queue(0, {OrderKind::Build, 0, 0, 0});
                clientGame.queue(1, {OrderKind::Produce, 19, 0, 0});
            }
            client.submit();
            host.submit();
            pump([&] { return clientGame.world().turn == turn + 1; });
            check(encodeWorld(hostGame.world()) == encodeWorld(clientGame.world()),
                  "Authoritative turn sync");
            sawEvents = sawEvents || !clientGame.log()->entries.empty();
        }
        check(sawEvents, "Remote events delivered");
        auto rawSend = [](sf::TcpSocket &socket, const std::string &body)
        {
            std::string frame(4, '\0');
            auto length = static_cast<std::uint32_t>(body.size());
            for (int i = 0; i < 4; ++i)
                frame[i] = static_cast<char>((length >> (24 - i * 8)) & 255);
            frame += body;
            check(socket.send(frame.data(), frame.size()) == sf::Socket::Status::Done, "Raw packet send");
        };
        GameFacade validatedGame;
        NetworkSession validatedHost(validatedGame);
        sf::TcpSocket invalidClient;
        validatedHost.host(0);
        check(invalidClient.connect(sf::IpAddress::LocalHost, validatedHost.port(), sf::seconds(1)) ==
                  sf::Socket::Status::Done,
              "Validation connection");
        rawSend(invalidClient, "HELLO 1");
        for (int n = 0; n < 1000 && !validatedHost.connected(); ++n)
        {
            validatedHost.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(validatedHost.connected(), "Validation handshake");
        auto unchanged = encodeWorld(validatedGame.world());
        rawSend(invalidClient,
                "ORDERS " + encodeOrders(1, {{OrderKind::Build, 19, 0, 0}, {OrderKind::Build, 0, 0, 0}}));
        for (int n = 0; n < 1000 && !validatedHost.failed(); ++n)
        {
            validatedHost.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(validatedHost.failed() && validatedGame.orders(1).empty() &&
                  encodeWorld(validatedGame.world()) == unchanged,
              "Invalid batch rejected atomically");
        sf::TcpSocket attacker;
        GameFacade safeGame;
        NetworkSession safeHost(safeGame);
        safeHost.host(0);
        check(attacker.connect(sf::IpAddress::LocalHost, safeHost.port(), sf::seconds(1)) ==
                  sf::Socket::Status::Done,
              "Test connection");
        const unsigned char badHeader[]{255, 255, 255, 255};
        check(attacker.send(badHeader, 4) == sf::Socket::Status::Done, "Send malformed length");
        for (int n = 0; n < 1000 && !safeHost.failed(); ++n)
        {
            safeHost.update();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(safeHost.failed(), "Oversized frame rejected before allocation");
        std::cout << "TCP handshake, 5 synchronized turns and hostile framing passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
