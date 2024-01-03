#include <cstdint>
#include <iostream>
#include <raylib.h>
#include <string>
#include <sockets/socket_lib.hpp>
#include <vector>
#include <cstddef>
#include <chrono>
#include <cstring>
#include <thread>
#include <mutex>
#include <array>

static constexpr auto colors = std::array{
        RED,
        GREEN,
        BLUE,
        YELLOW,
        PURPLE,
        LIME,
        GOLD,
        SKYBLUE,
        ORANGE,
        PINK
    };

static void send_position(c2k::ClientSocket& socket, int const x, int const y) {
    static_assert(sizeof(x) == 4);
    static_assert(sizeof(y) == 4);
    auto buffer = std::vector<std::byte>{};
    buffer.resize(8);
    std::memcpy(buffer.data(), &x, 4);
    std::memcpy(buffer.data() + 4, &y, 4);
    std::ignore = socket.send(std::move(buffer));
}

static std::mutex positions_mutex;
static std::unordered_map<int, std::pair<int, int>> client_positions;

static void read_incoming_positions(std::stop_token const& stop_token, c2k::ClientSocket& socket) {
    auto buffer = std::vector<std::byte>{};
    while (not stop_token.stop_requested()) {
        auto future = socket.receive(512);
        static constexpr auto timeout = std::chrono::milliseconds{ 100 };
        while (future.wait_for(timeout) == std::future_status::timeout) {
            if (stop_token.stop_requested()) {
                return;
            }
        }
        auto received_data = future.get();
        buffer.insert(buffer.end(), received_data.begin(), received_data.end());
        if (buffer.size() >= 4) {
            auto num_positions = 0;
            std::memcpy(&num_positions, buffer.data(), 4);
            auto const packet_size = 4 + num_positions * 3 * 4;
            while (buffer.size() < packet_size) {
                if (stop_token.stop_requested()) {
                    return;
                }
                received_data = socket.receive(512).get();
                buffer.insert(buffer.end(), received_data.begin(), received_data.end());
            }
            // std::cout << "received positions of " << num_positions << " clients\n";
            auto clients_received = std::vector<int>{};
            clients_received.reserve(num_positions);
            // discard first four bytes
            std::ranges::rotate(buffer, buffer.begin() + 4);
            buffer.resize(buffer.size() - 4);
            for (auto i = 0; i < num_positions; ++i) {
                assert(buffer.size() >= 12);
                auto client_id = 0;
                auto x = 0;
                auto y = 0;
                std::memcpy(&client_id, buffer.data(), 4);
                std::memcpy(&x, buffer.data() + 4, 4);
                std::memcpy(&y, buffer.data() + 8, 4);
                std::ranges::rotate(buffer, buffer.begin() + 12);
                buffer.resize(buffer.size() - 12);
                auto lock = std::scoped_lock{ positions_mutex };
                client_positions[client_id] = std::pair{ x, y };
                clients_received.push_back(client_id);
            }
            std::erase_if(
                client_positions,
                [&](auto const& pair) {
                    auto const find_iterator = std::ranges::find(clients_received, pair.first);
                    return find_iterator == clients_received.cend();
                }
            );
        }
    }
}

int main(int const argc, char** const argv) {
    if (argc != 3) {
        std::cout << "Usage: " << argv[0] << " [ip adress] [port]\n";
        return EXIT_FAILURE;
    }

    auto const ip_address = std::string{ argv[1] };
    auto const port = static_cast<uint16_t>(std::stoi(argv[2]));

    auto connection = c2k::SocketLib::create_client_socket(c2k::AddressFamily::Unspecified, ip_address, port);
    std::cout << "connected to server\n";

    static constexpr auto screen_width = 800;
    static constexpr auto screen_height = 450;

    auto circle_x = screen_width / 2;
    auto circle_y = screen_height / 2;

    auto last_send_time = std::chrono::steady_clock::now();

    InitWindow(screen_width, screen_height, "raylib Example");
    SetTargetFPS(60);

    auto update_thread = std::jthread(read_incoming_positions, std::ref(connection));

    while (!WindowShouldClose()) {
        if (IsKeyDown(KEY_RIGHT)) {
            circle_x += 2;
        }
        if (IsKeyDown(KEY_LEFT)) {
            circle_x -= 2;
        }
        if (IsKeyDown(KEY_UP)) {
            circle_y -= 2;
        }
        if (IsKeyDown(KEY_DOWN)) {
            circle_y += 2;
        }

        auto const current_time = std::chrono::steady_clock::now();
        auto const elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(current_time - last_send_time).count();
        if (elapsed >= 100) {
            send_position(connection, circle_x, circle_y);
            last_send_time = current_time;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawCircle(circle_x, circle_y, 20, DARKBLUE);

        {
            auto lock = std::scoped_lock{ positions_mutex };
            for (auto const& [client_id, client_position] : client_positions) {
                auto const color = colors.at(client_id % colors.size());
                DrawCircle(client_position.first, client_position.second, 20, color);
            }
        }

        EndDrawing();
    }
    update_thread.request_stop();
    CloseWindow();
}
