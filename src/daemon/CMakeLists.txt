# Keystone daemon — the production service binary.
#
# This target packages the Keystone transport daemon as a deployable binary. It
# depends on keystone_core (MessageBus, health check), keystone_transport
# (NatsConnection, NATSListener), and the nats.c library transitively.
#
# The daemon is installed to ${CMAKE_INSTALL_BINDIR}/keystone-server as part of
# the `keystone` component so that `cmake --install` (and the production
# Containerfile stage) can copy exactly this binary — and nothing else — into
# the production image.  See issue #513.

add_executable(keystone_server main.cpp)

target_include_directories(
  keystone_server PRIVATE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>)

target_link_libraries(
  keystone_server
  # keystone_agents is required: keystone_core's message_bus.cpp references the
  # typeinfo for keystone::agents::AgentCore (the bus stores AgentCore handles
  # in its registry), so the daemon binary must resolve those symbols at link
  # time.
  PRIVATE keystone_core keystone_transport keystone_agents)

set_target_properties(keystone_server PROPERTIES OUTPUT_NAME "keystone-server")

install(TARGETS keystone_server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                        COMPONENT keystone)
