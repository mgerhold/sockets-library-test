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
    auto extractor = c2k::Extractor{};
    while (socket.is_connected() and not stop_token.stop_requested()) {
        extractor << socket.receive(4096).get();
        while (auto const package = extractor.try_extract<int>()) {
            auto const num_positions = package.value();
            while (socket.is_connected() and not stop_token.stop_requested()
                   and extractor.size() < static_cast<std::size_t>(num_positions) * 3 * sizeof(int)) {
                extractor << socket.receive(4096).get();
            }
            if (not socket.is_connected() or stop_token.stop_requested()) {
                return;
            }
            auto clients_received = std::vector<int>{};
            clients_received.reserve(static_cast<std::size_t>(num_positions));
            auto lock = std::scoped_lock{ positions_mutex };
            for (auto i = 0; i < num_positions; ++i) {
                auto const [client_id, x, y] = extractor.try_extract<int, int, int>().value();
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
    std::ignore = socket.send(static_cast<int>(position.first), static_cast<int>(position.second));
}

int main(int const argc, char** const argv) try {
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
} catch (std::runtime_error const& e) {
    std::cerr << "error: " << e.what() << '\n';
}
