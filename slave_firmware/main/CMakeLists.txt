# CMakeLists.txt for component "main"

# List of source files for this component
set(COMPONENT_SRCS "main.c")

# List of include directories for this component
set(COMPONENT_ADD_INCLUDEDIRS "")

# Optional: Require other components.
#idf_component_register(SRCS "${COMPONENT_SRCS}"
#                       INCLUDE_DIRS "${COMPONENT_ADD_INCLUDEDIRS}"
#                       REQUIRES example_component)
#
# or if no other components are required:
idf_component_register(SRCS "${COMPONENT_SRCS}"
                       INCLUDE_DIRS "${COMPONENT_ADD_INCLUDEDIRS}"
                       PRIV_REQUIRES mdns)
