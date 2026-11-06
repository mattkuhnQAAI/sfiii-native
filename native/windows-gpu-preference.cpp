// Export the standard hybrid-GPU preference symbols from the executable.
// NVIDIA/AMD drivers read these before SDL creates its Vulkan device. They are
// deliberately Windows-only and do not select a specific vendor or adapter.
#ifdef _WIN32
#include <cstdint>
extern "C" {
__declspec(dllexport) std::uint32_t NvOptimusEnablement = 1;
__declspec(dllexport) std::uint32_t AmdPowerXpressRequestHighPerformance = 1;
}
#endif
