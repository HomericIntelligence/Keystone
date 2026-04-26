# Load the debug and release variables
file(GLOB DATA_FILES "${CMAKE_CURRENT_LIST_DIR}/cnats-*-data.cmake")

foreach(f ${DATA_FILES})
    include(${f})
endforeach()

# Create the targets for all the components
foreach(_COMPONENT ${cnats_COMPONENT_NAMES} )
    if(NOT TARGET ${_COMPONENT})
        add_library(${_COMPONENT} INTERFACE IMPORTED)
        message(${cnats_MESSAGE_MODE} "Conan: Component target declared '${_COMPONENT}'")
    endif()
endforeach()

if(NOT TARGET cnats::nats_static)
    add_library(cnats::nats_static INTERFACE IMPORTED)
    message(${cnats_MESSAGE_MODE} "Conan: Target declared 'cnats::nats_static'")
endif()
if(NOT TARGET cnats::nats_static)
    add_library(cnats::nats_static INTERFACE IMPORTED)
    set_property(TARGET cnats::nats_static PROPERTY INTERFACE_LINK_LIBRARIES cnats::nats_static)
endif()
# Load the debug and release library finders
file(GLOB CONFIG_FILES "${CMAKE_CURRENT_LIST_DIR}/cnats-Target-*.cmake")

foreach(f ${CONFIG_FILES})
    include(${f})
endforeach()