########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(cnats_COMPONENT_NAMES "")
if(DEFINED cnats_FIND_DEPENDENCY_NAMES)
  list(APPEND cnats_FIND_DEPENDENCY_NAMES OpenSSL)
  list(REMOVE_DUPLICATES cnats_FIND_DEPENDENCY_NAMES)
else()
  set(cnats_FIND_DEPENDENCY_NAMES OpenSSL)
endif()
set(OpenSSL_FIND_MODE "NO_MODULE")

########### VARIABLES #######################################################################
#############################################################################################
set(cnats_PACKAGE_FOLDER_RELEASE "/home/mvillmow/.conan2/p/b/cnats5928cc08180d8/p")
set(cnats_BUILD_MODULES_PATHS_RELEASE )


set(cnats_INCLUDE_DIRS_RELEASE "${cnats_PACKAGE_FOLDER_RELEASE}/include")
set(cnats_RES_DIRS_RELEASE )
set(cnats_DEFINITIONS_RELEASE )
set(cnats_SHARED_LINK_FLAGS_RELEASE )
set(cnats_EXE_LINK_FLAGS_RELEASE )
set(cnats_OBJECTS_RELEASE )
set(cnats_COMPILE_DEFINITIONS_RELEASE )
set(cnats_COMPILE_OPTIONS_C_RELEASE )
set(cnats_COMPILE_OPTIONS_CXX_RELEASE )
set(cnats_LIB_DIRS_RELEASE "${cnats_PACKAGE_FOLDER_RELEASE}/lib")
set(cnats_BIN_DIRS_RELEASE )
set(cnats_LIBRARY_TYPE_RELEASE STATIC)
set(cnats_IS_HOST_WINDOWS_RELEASE 0)
set(cnats_LIBS_RELEASE nats_static)
set(cnats_SYSTEM_LIBS_RELEASE pthread rt)
set(cnats_FRAMEWORK_DIRS_RELEASE )
set(cnats_FRAMEWORKS_RELEASE )
set(cnats_BUILD_DIRS_RELEASE )
set(cnats_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(cnats_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${cnats_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${cnats_COMPILE_OPTIONS_C_RELEASE}>")
set(cnats_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${cnats_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${cnats_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${cnats_EXE_LINK_FLAGS_RELEASE}>")


set(cnats_COMPONENTS_RELEASE )