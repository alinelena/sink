cmake_minimum_required(VERSION 2.8)


# ECM setup
find_package(ECM 0.0.10 REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH
        ${ECM_MODULE_PATH}
        #        ${ECM_KDE_MODULE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules
        ${CMAKE_MODULE_PATH})

include(FeatureSummary)
include(GenerateExportHeader)
#include(ECMSetupVersion)
include(ECMGenerateHeaders)
include(ECMPackageConfigHelpers)
# include(KDEInstallDirs)
# include(KDEFrameworkCompilerSettings)
# include(KDECMakeSettings)

find_package(Qt5Core REQUIRED)
find_package(FlatBuffers REQUIRED)
function(generate_flatbuffers)
    foreach(fbs ${ARGN})
       message("making ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h from ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs")
       add_custom_command(
              OUTPUT  ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h
              COMMAND ${FLATBUFFERS_FLATC_EXECUTABLE} -c -b --gen-includes -o ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs
              DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${fbs}.fbs
            )
       set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h PROPERTIES GENERATED 1)
       string(REGEX REPLACE "/" "_" target_name ${fbs})
       add_custom_target(generate_bindings${target_name} ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${fbs}_generated.h)
    endforeach(fbs)
endfunction(generate_flatbuffers)

set(CMAKE_AUTOMOC ON)
add_definitions("-Wall -std=c++0x")
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR} ${FLATBUFFERS_INCLUDE_DIR} ${CMAKE_BINARY_DIR}/common)
include_directories(SYSTEM ${CMAKE_SOURCE_DIR}/common)

configure_file(hawd.conf hawd.conf)

set(AKONADI2_RESOURCE_PLUGINS_PATH plugins/akonadi2/resources)
# common, eventually a lib but right now just the command buffers
add_subdirectory(common)

# the client
add_subdirectory(client)

# the synchronizer
add_subdirectory(synchronizer)

# a simple dummy resource implementation
add_subdirectory(dummyresource)

# some tests
add_subdirectory(tests)

# async library prototype
add_subdirectory(async)

feature_summary(WHAT ALL FATAL_ON_MISSING_REQUIRED_PACKAGES)
