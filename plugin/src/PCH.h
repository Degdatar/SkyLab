#pragma once

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <comdef.h>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <DirectXMath.h>

#include <wrl/client.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

namespace fs = std::filesystem;

using namespace std::literals;

#define DLLEXPORT __declspec(dllexport)

namespace logger = SKSE::log;

using SKSE::stl::report_and_fail;

template <class T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

namespace SkyLab
{
    class NifParser;
    class PreviewRenderer;
    class UIManager;
    class TextureManager;
}