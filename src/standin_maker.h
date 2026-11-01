// Builds the stand-in avatar from the game's data.pak (see standin_maker.cpp).
#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace dcc_avatar {

// Fills `out` with the contents of standin_avatar.bin. False on failure.
bool MakeStandinFromPak(const std::filesystem::path& pak_path, std::vector<uint8_t>& out);

}  // namespace dcc_avatar
