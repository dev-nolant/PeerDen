#pragma once

#include <cstdint>
#include <string>

namespace tunngle {

using TexID = uint32_t;

void TextureCacheInit(const std::string& api_base_url);
void TextureCacheShutdown();

void TextureCachePump();

TexID TextureCacheGet(const std::string& rawg_thumb_url);

TexID TextureCacheLoadLocal(const std::string& path);

TexID TextureCacheGetLogo();

}  // namespace tunngle
