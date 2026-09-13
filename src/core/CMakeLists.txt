# add the library for the core functionality of the code

add_library(core
    Order.cpp
)

target_include_directories(core PUBLIC
    "${PROJECT_SOURCE_DIR}/include"
)