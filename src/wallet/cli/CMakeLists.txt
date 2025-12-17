# CMakeLists.txt for the wallet CLI

# Set the minimum required version of CMake
cmake_minimum_required(VERSION 3.18)

# Define the executable
add_executable(wallet_cli main.cpp)

# Include directories are inherited from the chronos_lib target
# so we don't need to specify them here again.

# Link against the shared chronos_lib library
target_link_libraries(wallet_cli PUBLIC chronos_lib)
