# CMakeLists.txt for project root

cmake_minimum_required(VERSION 3.16)

# Project name (can be anything)
project(slave_firmware)

# Include ESP-IDF CMake functions
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# This is the only component in this project.
# If you have more components, add them here.
# By default, ESP-IDF expects the main application source code to be
# in a component named "main", located in a subdirectory "main".
set(COMPONENT_DIRS "main")

# If your project has extra components located in a subdirectory
# (e.g. "components"), then uncomment the following line:
# set(EXTRA_COMPONENT_DIRS components)

# Optional: Specify a custom components directory.
# By default, ESP-IDF searches for components in $IDF_PATH/components,
# project_dir/components and EXTRA_COMPONENT_DIRS.
# set(COMPONENTS_DIRS "") # Clear default components dirs
# list(APPEND COMPONENTS_DIRS "my_components_dir") # Add a new one

# Optional: List of components to be excluded from the build.
# set(EXCLUDE_COMPONENTS "")
