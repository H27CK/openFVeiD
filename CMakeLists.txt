cmake_minimum_required(VERSION 3.15)
project(FVDPlusPlus VERSION 0.9.3 LANGUAGES C CXX)

set(FVD_VERSION "" CACHE STRING "Version string shown in the app, injected by the release workflow")
if(FVD_VERSION STREQUAL "")
    execute_process(COMMAND git describe --tags --always --dirty
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE FVD_VERSION OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if(FVD_VERSION STREQUAL "")
    set(FVD_VERSION "v${PROJECT_VERSION}-dev")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(WIN32)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# --- Dependencies ---
include(FetchContent)

# GLFW (Windowing)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
FetchContent_MakeAvailable(glfw)

# Dear ImGui (UI)
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG docking)
FetchContent_MakeAvailable(imgui)

# ImGui manual library setup
add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
if(MSVC)
    target_compile_options(imgui PRIVATE /w)
else()
    target_compile_options(imgui PRIVATE -w)
endif()

# ImPlot (Graphs)
FetchContent_Declare(implot GIT_REPOSITORY https://github.com/epezent/implot.git GIT_TAG master)
FetchContent_MakeAvailable(implot)

# --- PATCH IMPLOT FOR 4 Y-AXES ---
file(READ "${implot_SOURCE_DIR}/implot.h" IMPLOT_HEADER)
if(NOT IMPLOT_HEADER MATCHES "ImAxis_Y4")
    string(REPLACE "ImAxis_Y3,     // disabled by default" "ImAxis_Y3,     // disabled by default\n    ImAxis_Y4,     // patched in by FVD++" IMPLOT_HEADER "${IMPLOT_HEADER}")
    file(WRITE "${implot_SOURCE_DIR}/implot.h" "${IMPLOT_HEADER}")
endif()

add_library(implot STATIC
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
    ${implot_SOURCE_DIR}/implot_demo.cpp
)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PRIVATE imgui) # Critical: ImPlot needs ImGui
if(MSVC)
    target_compile_options(implot PRIVATE /w)
else()
    target_compile_options(implot PRIVATE -w)
endif()

# GLM
if(WIN32)
    find_package(glm CONFIG REQUIRED)
else()
    find_package(glm QUIET)
    if(NOT glm_FOUND)
        set(DEPS_ROOT "/path/to/dependencies")
        set(GLM_INCLUDE_DIR "${DEPS_ROOT}/glm-1.0.3")
    endif()
endif()

# Vulkan (renderer) + glslangValidator (SPIR-V compilation)
find_package(Vulkan REQUIRED COMPONENTS glslangValidator)

# --- Vulkan shader compilation ---
file(GLOB VULKAN_SHADER_SOURCES "shaders/vulkan/*.vert" "shaders/vulkan/*.frag")
set(VULKAN_SHADER_OUTPUTS "")
foreach(SHADER_SOURCE ${VULKAN_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER_SOURCE} NAME)
    set(SHADER_OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shaders_vulkan/${SHADER_NAME}.spv)
    add_custom_command(
        OUTPUT ${SHADER_OUTPUT}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders_vulkan
        COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V ${SHADER_SOURCE} -o ${SHADER_OUTPUT}
        DEPENDS ${SHADER_SOURCE}
        COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
    )
    list(APPEND VULKAN_SHADER_OUTPUTS ${SHADER_OUTPUT})
endforeach()
add_custom_target(vulkan_shaders DEPENDS ${VULKAN_SHADER_OUTPUTS})

# --- Embedded Assets ---
add_executable(bin2c tools/bin2c.cpp)
file(GLOB ASSETS_FONTS RELATIVE ${CMAKE_SOURCE_DIR} "fonts/*")
set(ASSETS ${ASSETS_FONTS})

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embedded_assets.cpp
    COMMAND bin2c ${CMAKE_CURRENT_BINARY_DIR}/embedded_assets.cpp ${ASSETS}
    DEPENDS bin2c ${ASSETS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMENT "Generating embedded assets"
)

# --- Sources ---
file(GLOB_RECURSE CORE_SOURCES "core/*.cpp")
set(RENDERER_SOURCES
    renderer/spirvlocator.cpp
    renderer/trackmesh.cpp
    renderer/viewport.cpp
    renderer/vulkan/vulkanbuffer.cpp
    renderer/vulkan/vulkancontext.cpp
    renderer/vulkan/vulkanframebuffer.cpp
    renderer/vulkan/vulkanpipeline.cpp
    renderer/vulkan/vulkantexture.cpp
)

if(WIN32)
    set(RES_FILE resources.rc)
endif()

add_executable(fvd
    main.cpp
    ${CORE_SOURCES}
    ${RENDERER_SOURCES}
    ui/graphview.cpp
    ui/resultinggraphprocessor.cpp
    ui/measurementgraphprocessor.cpp
    ui/leftpanel.cpp
    ui/transitionview.cpp
    ${RES_FILE}
    ${CMAKE_CURRENT_BINARY_DIR}/embedded_assets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
add_dependencies(fvd vulkan_shaders)

execute_process(COMMAND git rev-parse --short HEAD OUTPUT_VARIABLE GIT_COMMIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE)
target_compile_definitions(fvd PRIVATE
    GIT_COMMIT_HASH="${GIT_COMMIT_HASH}"
    FVD_VERSION="${FVD_VERSION}"
    FVD_SPIRV_DIR="${CMAKE_CURRENT_BINARY_DIR}/shaders_vulkan"
    GLFW_INCLUDE_NONE
)
target_include_directories(fvd PRIVATE
    .
    core/
    renderer/
    ext/
    ${GLM_INCLUDE_DIR}
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
)
if(TARGET glm::glm)
    target_link_libraries(fvd PRIVATE glm::glm)
endif()

# OS Specific libs
if(UNIX AND NOT APPLE)
    target_link_libraries(fvd PRIVATE
        glfw
        imgui
        implot
        Vulkan::Vulkan
        X11
        pthread
        dl
    )
    target_link_options(fvd PRIVATE -rdynamic)
elseif(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
    target_link_libraries(fvd PRIVATE
        glfw
        imgui
        implot
        Vulkan::Vulkan
        ${COREFOUNDATION_LIBRARY}
    )
    set_target_properties(fvd PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "FVD++"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.openfvd.fvdplusplus"
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist.in
    )
    add_custom_command(TARGET fvd POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/track_styles
            $<TARGET_BUNDLE_CONTENT_DIR:fvd>/Resources/track_styles
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_BINARY_DIR}/shaders_vulkan
            $<TARGET_BUNDLE_CONTENT_DIR:fvd>/Resources/shaders_vulkan
        COMMAND ${CMAKE_COMMAND} -E copy
            ${CMAKE_SOURCE_DIR}/icons/fvd_orig.icns
            $<TARGET_BUNDLE_CONTENT_DIR:fvd>/Resources/fvd.icns
        COMMENT "Bundling track styles, shaders and icon"
    )
elseif(WIN32)
    target_link_libraries(fvd PRIVATE
        glfw
        imgui
        implot
        Vulkan::Vulkan
        dbghelp
        winmm
    )
    target_compile_definitions(fvd PRIVATE NOMINMAX)
endif()
