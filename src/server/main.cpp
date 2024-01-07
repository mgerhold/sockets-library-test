#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <future>
#include <iostream>
#include <mutex>
#include <sockets/sockets.hpp>
#include <thread>
#include <unordered_map>

static std::mutex positions_mutex;
static std::unordered_map<int, std::pair<int, int>> client_positions;

static std::mutex clients_mutex;
static std::unordered_map<int, std::unique_ptr<c2k::ClientSocket>> active_clients;

static void receive_client_positions(int const id, c2k::ClientSocket& client) {
    auto extractor = c2k::Extractor{};
    while (client.is_connected()) {
        extractor << client.receive(4096).get();
        while (extractor.size() >= 2 * sizeof(int)) {
            auto const [x, y] = extractor.try_extract<int, int>().value();
            {
                auto lock = std::scoped_lock{ positions_mutex };
                client_positions[id] = { x, y };
            }
        }
    }
    // connection ended
    {
        auto lock = std::scoped_lock{ positions_mutex, clients_mutex };
        client_positions.erase(id);
        active_clients.erase(id);
        std::cout << std::format("client with id {} disconnected\n", id);
    }
}

static void broadcast_positions() {
    while (true) {
        auto buffer = c2k::MessageBuffer{};
        {
            auto lock = std::scoped_lock{ positions_mutex };
            buffer << static_cast<int>(client_positions.size());
            for (auto const& [client_id, client_position] : client_positions) {
                buffer << client_id << client_position.first << client_position.second;
            }
        }
        for (auto const& [id, connection] : active_clients) {
            std::ignore = connection->send(buffer);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main() try {
    auto next_client_id = std::atomic_int{ 0 };
    auto receive_thread = std::jthread{ broadcast_positions };
    auto accept_client = [&next_client_id](c2k::ClientSocket client_connection) {
        auto lock = std::scoped_lock{ clients_mutex };
        auto const current_client_id = next_client_id++;
        std::cout << "client with id " << current_client_id << " connected\n";
        std::cout << client_connection.remote_address() << '\n';
        auto socket = std::make_unique<c2k::ClientSocket>(std::move(client_connection));
        std::jthread{ receive_client_positions, current_client_id, std::ref(*socket) }.detach();
        active_clients[current_client_id] = std::move(socket);
    };

    auto const ipv4_server = c2k::Sockets::create_server(c2k::AddressFamily::Ipv4, 12345, accept_client);
    auto const ipv6_server = c2k::Sockets::create_server(c2k::AddressFamily::Ipv6, 12345, accept_client);
    std::cout << "listening for incoming client connections...\n";

    std::promise<void>{}.get_future().wait();
} catch (std::runtime_error const& e) {
    std::cerr << "error: " << e.what() << '\n';
}
