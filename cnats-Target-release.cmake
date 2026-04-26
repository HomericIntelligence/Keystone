# Avoid multiple calls to find_package to append duplicated properties to the targets
include_guard()########### VARIABLES #######################################################################
#############################################################################################
set(cnats_FRAMEWORKS_FOUND_RELEASE "") # Will be filled later
conan_find_apple_frameworks(cnats_FRAMEWORKS_FOUND_RELEASE "${cnats_FRAMEWORKS_RELEASE}" "${cnats_FRAMEWORK_DIRS_RELEASE}")

set(cnats_LIBRARIES_TARGETS "") # Will be filled later


######## Create an interface target to contain all the dependencies (frameworks, system and conan deps)
if(NOT TARGET cnats_DEPS_TARGET)
    add_library(cnats_DEPS_TARGET INTERFACE IMPORTED)
endif()

set_property(TARGET cnats_DEPS_TARGET
             APPEND PROPERTY INTERFACE_LINK_LIBRARIES
             $<$<CONFIG:Release>:${cnats_FRAMEWORKS_FOUND_RELEASE}>
             $<$<CONFIG:Release>:${cnats_SYSTEM_LIBS_RELEASE}>
             $<$<CONFIG:Release>:openssl::openssl>)

####### Find the libraries declared in cpp_info.libs, create an IMPORTED target for each one and link the
####### cnats_DEPS_TARGET to all of them
conan_package_library_targets("${cnats_LIBS_RELEASE}"    # libraries
                              "${cnats_LIB_DIRS_RELEASE}" # package_libdir
                              "${cnats_BIN_DIRS_RELEASE}" # package_bindir
                              "${cnats_LIBRARY_TYPE_RELEASE}"
                              "${cnats_IS_HOST_WINDOWS_RELEASE}"
                              cnats_DEPS_TARGET
                              cnats_LIBRARIES_TARGETS  # out_libraries_targets
                              "_RELEASE"
                              "cnats"    # package_name
                              "${cnats_NO_SONAME_MODE_RELEASE}")  # soname

# FIXME: What is the result of this for multi-config? All configs adding themselves to path?
set(CMAKE_MODULE_PATH ${cnats_BUILD_DIRS_RELEASE} ${CMAKE_MODULE_PATH})

########## GLOBAL TARGET PROPERTIES Release ########################################
    set_property(TARGET cnats::nats_static
                 APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                 $<$<CONFIG:Release>:${cnats_OBJECTS_RELEASE}>
                 $<$<CONFIG:Release>:${cnats_LIBRARIES_TARGETS}>
                 )

    if("${cnats_LIBS_RELEASE}" STREQUAL "")
        # If the package is not declaring any "cpp_info.libs" the package deps, system libs,
        # frameworks etc are not linked to the imported targets and we need to do it to the
        # global target
        set_property(TARGET cnats::nats_static
                     APPEND PROPERTY INTERFACE_LINK_LIBRARIES
                     cnats_DEPS_TARGET)
    endif()

    set_property(TARGET cnats::nats_static
                 APPEND PROPERTY INTERFACE_LINK_OPTIONS
                 $<$<CONFIG:Release>:${cnats_LINKER_FLAGS_RELEASE}>)
    set_property(TARGET cnats::nats_static
                 APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES
                 $<$<CONFIG:Release>:${cnats_INCLUDE_DIRS_RELEASE}>)
    # Necessary to find LINK shared libraries in Linux
    set_property(TARGET cnats::nats_static
                 APPEND PROPERTY INTERFACE_LINK_DIRECTORIES
                 $<$<CONFIG:Release>:${cnats_LIB_DIRS_RELEASE}>)
    set_property(TARGET cnats::nats_static
                 APPEND PROPERTY INTERFACE_COMPILE_DEFINITIONS
                 $<$<CONFIG:Release>:${cnats_COMPILE_DEFINITIONS_RELEASE}>)
    set_property(TARGET cnats::nats_static
                 APPEND PROPERTY INTERFACE_COMPILE_OPTIONS
                 $<$<CONFIG:Release>:${cnats_COMPILE_OPTIONS_RELEASE}>)

########## For the modules (FindXXX)
set(cnats_LIBRARIES_RELEASE cnats::nats_static)
