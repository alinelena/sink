project(akonadi2_synchronizer)

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})

set(akonadi2synchronizer_SRCS
    main.cpp
    listener.cpp
)

add_executable(${PROJECT_NAME} ${akonadi2synchronizer_SRCS})
target_link_libraries(${PROJECT_NAME} akonadi2common)
qt5_use_modules(${PROJECT_NAME} Widgets Network)
install(TARGETS ${PROJECT_NAME} DESTINATION bin)
