add_library(fake_udp_client fake_udp_client.cpp)
target_include_directories(fake_udp_client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_library(spy_mqtt_broker spy_mqtt_broker.cpp)
target_include_directories(spy_mqtt_broker PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
