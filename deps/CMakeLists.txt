include(get_cpm.cmake)

CPMAddPackage("gh:g-truc/glm#1.0.1")
CPMAddPackage("gh:TsudaKageyu/minhook@1.3.4")

# mnemosyne adds its test/ and bench/ subdirectories unconditionally, and its test
# CMakeLists fetches googletest at GIT_TAG main. A moving tag makes CMake re-contact
# github on every configure, so a machine that is offline -- or just rate-limited --
# fails the whole build on a dependency we never link against.
#
# Declaring googletest first at a pinned commit wins: FetchContent honours the first
# declaration, so mnemosyne's is ignored and the pinned one is reused from cache.
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest
        GIT_TAG 3940de91897160fea4815998e08d0fa3c2fb077e
        GIT_SHALLOW TRUE
        FIND_PACKAGE_ARGS NAMES gtest_main
)
# Keep gtest off the install set and out of our warning surface.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

CPMAddPackage("gh:brampedgex/mnemosyne#48078d302fa3ba9e22b80ca69eadf27811caf361")
CPMAddPackage(
        NAME json
        VERSION 3.12.0
        URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
        URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa
)
CPMAddPackage("gh:BasedInc/libhat@0.6.0")
CPMAddPackage("gh:skypjack/entt#fe8d7d78c4823e8a66a050bf86f5c6318cf76ce7")

target_include_directories(Necromancer PRIVATE
	"include"
)

target_link_libraries(Necromancer PRIVATE
	minhook
	glm::glm
	mnemosyne
	nlohmann_json
	libhat::libhat
	EnTT::EnTT
)
