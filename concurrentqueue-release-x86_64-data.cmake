########### AGGREGATED COMPONENTS AND DEPENDENCIES FOR THE MULTI CONFIG #####################
#############################################################################################

set(concurrentqueue_COMPONENT_NAMES "")
if(DEFINED concurrentqueue_FIND_DEPENDENCY_NAMES)
  list(APPEND concurrentqueue_FIND_DEPENDENCY_NAMES )
  list(REMOVE_DUPLICATES concurrentqueue_FIND_DEPENDENCY_NAMES)
else()
  set(concurrentqueue_FIND_DEPENDENCY_NAMES )
endif()

########### VARIABLES #######################################################################
#############################################################################################
set(concurrentqueue_PACKAGE_FOLDER_RELEASE "/home/mvillmow/.conan2/p/concu0e8c745f17630/p")
set(concurrentqueue_BUILD_MODULES_PATHS_RELEASE )


set(concurrentqueue_INCLUDE_DIRS_RELEASE "${concurrentqueue_PACKAGE_FOLDER_RELEASE}/include"
			"${concurrentqueue_PACKAGE_FOLDER_RELEASE}/include/moodycamel")
set(concurrentqueue_RES_DIRS_RELEASE )
set(concurrentqueue_DEFINITIONS_RELEASE )
set(concurrentqueue_SHARED_LINK_FLAGS_RELEASE )
set(concurrentqueue_EXE_LINK_FLAGS_RELEASE )
set(concurrentqueue_OBJECTS_RELEASE )
set(concurrentqueue_COMPILE_DEFINITIONS_RELEASE )
set(concurrentqueue_COMPILE_OPTIONS_C_RELEASE )
set(concurrentqueue_COMPILE_OPTIONS_CXX_RELEASE )
set(concurrentqueue_LIB_DIRS_RELEASE )
set(concurrentqueue_BIN_DIRS_RELEASE )
set(concurrentqueue_LIBRARY_TYPE_RELEASE UNKNOWN)
set(concurrentqueue_IS_HOST_WINDOWS_RELEASE 0)
set(concurrentqueue_LIBS_RELEASE )
set(concurrentqueue_SYSTEM_LIBS_RELEASE pthread)
set(concurrentqueue_FRAMEWORK_DIRS_RELEASE )
set(concurrentqueue_FRAMEWORKS_RELEASE )
set(concurrentqueue_BUILD_DIRS_RELEASE )
set(concurrentqueue_NO_SONAME_MODE_RELEASE FALSE)


# COMPOUND VARIABLES
set(concurrentqueue_COMPILE_OPTIONS_RELEASE
    "$<$<COMPILE_LANGUAGE:CXX>:${concurrentqueue_COMPILE_OPTIONS_CXX_RELEASE}>"
    "$<$<COMPILE_LANGUAGE:C>:${concurrentqueue_COMPILE_OPTIONS_C_RELEASE}>")
set(concurrentqueue_LINKER_FLAGS_RELEASE
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,SHARED_LIBRARY>:${concurrentqueue_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,MODULE_LIBRARY>:${concurrentqueue_SHARED_LINK_FLAGS_RELEASE}>"
    "$<$<STREQUAL:$<TARGET_PROPERTY:TYPE>,EXECUTABLE>:${concurrentqueue_EXE_LINK_FLAGS_RELEASE}>")


set(concurrentqueue_COMPONENTS_RELEASE )