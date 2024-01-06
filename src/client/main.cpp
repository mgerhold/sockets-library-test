#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <raylib.h>
#include <sockets/sockets.hpp>
#include <string>
#include <thread>
#include <vector>

static constexpr auto colors = std::array{ RED, GREEN, BLUE, YELLOW, PURPLE, LIME, GOLD, SKYBLUE, ORANGE, PINK };

static std::mutex positions_mutex;
static std::unordered_map<int, std::pair<int, int>> client_positions;

static void read_incoming_positions(std::stop_token const& stop_token, c2k::ClientSocket& socket) {
    auto buffer = std::vector<std::byte>{};
    while (socket.is_connected() and not stop_token.stop_requested()) {
        auto received_data = socket.receive(4096).get();
        buffer.insert(buffer.end(), received_data.begin(), received_data.end());
        while (buffer.size() >= 4) {
            auto num_positions = 0;
            std::memcpy(&num_positions, buffer.data(), 4);
            auto const packet_size = 4 + num_positions * 3 * 4;
            while (socket.is_connected() and buffer.size() < static_cast<std::size_t>(packet_size)) {
                if (stop_token.stop_requested()) {
                    return;
                }
                received_data = socket.receive(4096).get();
                buffer.insert(buffer.end(), received_data.begin(), received_data.end());
            }
            // std::cout << std::format("received positions of {} clients\n", num_positions);
            auto clients_received = std::vector<int>{};
            clients_received.reserve(static_cast<std::size_t>(num_positions));
            // discard first four bytes
            std::rotate(buffer.begin(), buffer.begin() + 4, buffer.end());
            buffer.resize(buffer.size() - 4);
            for (auto i = 0; i < num_positions; ++i) {
                assert(buffer.size() >= 12);
                auto client_id = 0;
                auto x = 0;
                auto y = 0;
                std::memcpy(&client_id, buffer.data(), 4);
                std::memcpy(&x, buffer.data() + 4, 4);
                std::memcpy(&y, buffer.data() + 8, 4);
                std::rotate(buffer.begin(), buffer.begin() + 12, buffer.end());
                buffer.resize(buffer.size() - 12);
                auto lock = std::scoped_lock{ positions_mutex };
                client_positions[client_id] = std::pair{ x, y };
                clients_received.push_back(client_id);
            }
            std::erase_if(client_positions, [&](auto const& pair) {
                auto const find_iterator = std::ranges::find(clients_received, pair.first);
                return find_iterator == clients_received.cend();
            });
        }
    }
}

static void send_position(c2k::ClientSocket& socket, std::pair<double, double> position) {
    auto const x = static_cast<int>(position.first);
    auto const y = static_cast<int>(position.second);
    static_assert(sizeof(x) == 4);
    static_assert(sizeof(y) == 4);
    auto buffer = std::vector<std::byte>{};
    buffer.resize(8);
    std::memcpy(buffer.data(), &x, 4);
    std::memcpy(buffer.data() + 4, &y, 4);
    std::ignore = socket.send(std::move(buffer));
}

int main(int const argc, char** const argv) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " [ip adress] [port]\n";
        return EXIT_FAILURE;
    }

    auto const ip_address = std::string{ argv[1] };
    auto const port = static_cast<uint16_t>(std::stoi(argv[2]));

    auto connection = c2k::Sockets::create_client(c2k::AddressFamily::Unspecified, ip_address, port);
    std::cout << "connected to server\n";

    static constexpr auto screen_width = 800;
    static constexpr auto screen_height = 450;

    auto circle_x = static_cast<double>(screen_width) / 2.0;
    auto circle_y = static_cast<double>(screen_height) / 2.0;

    InitWindow(screen_width, screen_height, "sockets test");
    SetTargetFPS(60);

    auto update_thread = std::jthread(read_incoming_positions, std::ref(connection));

    auto last_time = GetTime();
    while (!WindowShouldClose()) {
        auto const delta = [&]() {
            auto const current_time = GetTime();
            auto const result = current_time - last_time;
            last_time = current_time;
            return result;
        }();
        static constexpr auto pixels_per_second = 100.0;
        if (IsKeyDown(KEY_RIGHT)) {
            circle_x += pixels_per_second * delta;
        }
        if (IsKeyDown(KEY_LEFT)) {
            circle_x -= pixels_per_second * delta;
        }
        if (IsKeyDown(KEY_UP)) {
            circle_y -= pixels_per_second * delta;
        }
        if (IsKeyDown(KEY_DOWN)) {
            circle_y += pixels_per_second * delta;
        }

        send_position(connection, { circle_x, circle_y });

        BeginDrawing();
        ClearBackground(CLITERAL(Color){ 36, 39, 58, 255 });

        {
            auto lock = std::scoped_lock{ positions_mutex };
            for (auto const& [client_id, client_position] : client_positions) {
                auto const color = colors.at(static_cast<std::size_t>(client_id) % colors.size());
                DrawCircle(client_position.first, client_position.second, 20, color);
            }
        }

        EndDrawing();
    }
    update_thread.request_stop();
    CloseWindow();
}
