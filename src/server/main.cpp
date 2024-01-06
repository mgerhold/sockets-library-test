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

static std::pair<int, int> extract_position(std::array<std::byte, 8> const& bytes) {
    auto x = 0;
    auto y = 0;
    static_assert(sizeof(x) + sizeof(y) == std::tuple_size_v<std::remove_cvref_t<decltype(bytes)>>);

    // we ignore endianess here!
    std::memcpy(&x, &bytes.at(0), sizeof(x));
    std::memcpy(&y, &bytes.at(4), sizeof(y));

    return { x, y };
}

static std::mutex positions_mutex;
static std::unordered_map<int, std::pair<int, int>> client_positions;

static std::mutex clients_mutex;
static std::unordered_map<int, std::unique_ptr<c2k::ClientSocket>> active_clients;

static void receive_client_positions(int const id, c2k::ClientSocket& client) {
    auto receive_buffer = std::vector<std::byte>{};
    while (client.is_connected()) {
        auto const received_data = client.receive(4096).get();
        receive_buffer.insert(receive_buffer.end(), received_data.begin(), received_data.end());
        while (receive_buffer.size() >= 8) {
            auto first_eight_bytes = std::array<std::byte, 8>{};
            std::copy_n(receive_buffer.begin(), first_eight_bytes.size(), first_eight_bytes.begin());
            auto const position = extract_position(first_eight_bytes);
            {
                auto lock = std::scoped_lock{ positions_mutex };
                client_positions[id] = position;
            }
            std::rotate(
                    receive_buffer.begin(),
                    receive_buffer.begin() + first_eight_bytes.size(),
                    receive_buffer.end()
            );
            receive_buffer.resize(receive_buffer.size() - first_eight_bytes.size());
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
        auto send_buffer = std::vector<std::byte>{};
        {
            auto lock = std::scoped_lock{ positions_mutex };
            auto const num_positions = static_cast<int>(client_positions.size());
            send_buffer.resize(4 + static_cast<std::size_t>(num_positions) * (4 + 4 + 4));
            std::memcpy(send_buffer.data(), &num_positions, 4);
            auto offset = std::size_t{ 4 };
            for (auto const& [client_id, client_position] : client_positions) {
                std::memcpy(send_buffer.data() + offset, &client_id, 4);
                std::memcpy(send_buffer.data() + offset + 4, &client_position.first, 4);
                std::memcpy(send_buffer.data() + offset + 8, &client_position.second, 4);
                offset += 12;
            }
        }

        {
            auto lock = std::scoped_lock{ clients_mutex };
            for (auto const& [id, connection] : active_clients) {
                // std::cout << std::format("sending {} positions to client {}\n", (send_buffer.size() - 4) / 3 / 4, id);
                std::ignore = connection->send(send_buffer);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

int main() try {
    auto next_client_id = std::atomic_int{ 0 };
    auto receive_thread = std::jthread{ broadcast_positions };
    auto server = c2k::Sockets::create_server(
            c2k::AddressFamily::Unspecified,
            12345,
            [&next_client_id](c2k::ClientSocket client_connection) {
                auto lock = std::scoped_lock{ clients_mutex };
                auto const current_client_id = next_client_id++;
                std::cout << "client with id " << current_client_id << " connected\n";
                auto socket = std::make_unique<c2k::ClientSocket>(std::move(client_connection));
                std::jthread{ receive_client_positions, current_client_id, std::ref(*socket) }.detach();
                active_clients[current_client_id] = std::move(socket);
            }
    );
    std::cout << "listening for incoming client connections...\n";

    std::promise<void>{}.get_future().wait();
} catch (std::runtime_error const& e) {
    std::cerr << "error: " << e.what() << '\n';
}
