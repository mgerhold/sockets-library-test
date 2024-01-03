#include <array>
#include <future>
#include <iostream>
#include <thread>
#include <sockets/socket_lib.hpp>
#include <atomic>
#include <unordered_map>
#include <mutex>
#include <cstring>

static std::mutex positions_mutex;
static std::unordered_map<int, std::pair<int, int>> client_positions;

static std::pair<int, int> extract_position(std::array<std::byte, 8> const& bytes) {
    auto x = 0;
    auto y = 0;
    static_assert(sizeof(x) + sizeof(y) == std::tuple_size_v<std::remove_cvref_t<decltype(bytes)>>);

    // we ignore endianess here!
    std::memcpy(&x, &bytes.at(0), sizeof(x));
    std::memcpy(&y, &bytes.at(4), sizeof(y));

    return { x, y };
}

static void handle_client(c2k::ClientSocket client, int const id) {
    std::cout << "client connected\n";
    auto receive_buffer = std::vector<std::byte>{};
    while (client.is_connected()) {
        auto const received_data = client.receive(512).get();
        receive_buffer.insert(receive_buffer.end(), received_data.begin(), received_data.end());
        if (receive_buffer.size() >= 8) {
            auto first_eight_bytes = std::array<std::byte, 8>{};
            std::copy_n(receive_buffer.begin(), first_eight_bytes.size(), first_eight_bytes.begin());
            auto const position = extract_position(first_eight_bytes);
            {
                auto lock = std::scoped_lock{ positions_mutex };
                client_positions[id] = position;
            }
            std::ranges::rotate(receive_buffer, receive_buffer.begin() + first_eight_bytes.size());
            receive_buffer.resize(receive_buffer.size() - first_eight_bytes.size());

            auto send_buffer = std::vector<std::byte>{};
            {
                auto lock = std::scoped_lock{ positions_mutex };
                auto const num_positions = static_cast<int>(client_positions.size() - 1);
                send_buffer.resize(4 + num_positions * (4 + 4 + 4));
                std::memcpy(send_buffer.data(), &num_positions, 4);
                auto offset = std::size_t{ 4 };
                for (auto const& [client_id, client_position] : client_positions) {
                    if (client_id == id) {
                        // don't send the position of a client back to the same client
                        continue;
                    }
                    std::memcpy(send_buffer.data() + offset, &client_id, 4);
                    std::memcpy(send_buffer.data() + offset + 4, &client_position.first, 4);
                    std::memcpy(send_buffer.data() + offset + 8, &client_position.second, 4);
                    offset += 12;
                }
            }
            std::ignore = client.send(std::move(send_buffer));
        }
    }
    std::cout << "client has disconnected\n";
    auto lock = std::scoped_lock{ positions_mutex };
    client_positions.erase(id);
}

int main() {
    auto next_client_id = std::atomic_int{ 0 };
    auto server = c2k::SocketLib::create_server_socket(
        c2k::AddressFamily::Unspecified,
        12345,
        [&next_client_id](c2k::ClientSocket client_connection) {
            std::jthread{ handle_client, std::move(client_connection), next_client_id++ }.detach();
        }
    );
    std::cout << "listening for incoming client connections...\n";

    std::promise<void>{}.get_future().wait();
}
