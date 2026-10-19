# Create a dummy export set so CommonLibSSE's install(EXPORT CommonLibSSE-targets ...)
# doesn't error about spdlog not being in any export set when spdlog is fetched
# with OVERRIDE_FIND_PACKAGE (no install export). We just make spdlog's install
# not require the export, by aliasing its export name.
# Workaround: pre-include runs at top of each subproject's CMakeLists scope.
# If the project is spdlog itself we patch it, if CommonLibSSE we ensure its
# export dependency is satisfied.
if(PROJECT_NAME STREQUAL "CommonLibSSE")
  # CommonLibSSE install() will fail if spdlog is not in an export set.
  # Define one on the fly by exporting spdlog targets to CommonLibSSE-targets
  # alternative: disable CommonLibSSE install entirely.
  set(CMAKE_INSTALL_DEFAULT_COMPONENT_NAME "Unspecified" CACHE STRING "" FORCE)
endif()
if(PROJECT_NAME STREQUAL "spdlog")
  # Don't let spdlog try to install its export — we don't need it
  set(SPDLOG_INSTALL OFF CACHE BOOL "" FORCE)
endif()
