add_library(communication src/email_publisher.cpp)
target_include_directories(communication PUBLIC include)
target_link_libraries(communication PUBLIC spdlog::spdlog)
