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
  # Pure transport: the MessageBus registry now stores core::IMessageSink
  # handles (ADR-015 decoupling, #577), so the daemon no longer links the agent
  # layer — that hierarchy was extracted to ProjectAgamemnon.
  PRIVATE keystone_core keystone_transport)

set_target_properties(keystone_server PROPERTIES OUTPUT_NAME "keystone-server")

install(TARGETS keystone_server RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                                        COMPONENT keystone)
