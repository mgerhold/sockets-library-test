include(${PROJECT_SOURCE_DIR}/cmake/CPM.cmake)

function(setup_dependencies)
    CPMAddPackage(
            NAME RAYLIB
            GITHUB_REPOSITORY raysan5/raylib
            GIT_TAG 5.0
    )
    CPMAddPackage(
            NAME C2K_SOCKETS
            GITHUB_REPOSITORY mgerhold/sockets
            VERSION 0.1.6
            OPTIONS
            "enable_thread_sanitizer ${enable_thread_sanitizer}"
            "enable_address_sanitizer ${enable_address_sanitizer}"
            "enable_undefined_behavior_sanitizer ${enable_undefined_behavior_sanitizer}"
    )
endfunction()
