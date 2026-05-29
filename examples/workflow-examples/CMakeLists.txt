# CMakeLists.txt for ProjectKeystone Workflow Examples
#
# NOTE: All workflow example source files have been removed per ADR-015.
# The thread-based, IPC-based, and network-based examples demonstrated the
# HMAS agent hierarchy (ChiefArchitectAgent, ComponentLeadAgent,
# ModuleLeadAgent, TaskAgent), which has been extracted to ProjectAgamemnon.
#
# This file is retained as a placeholder. When ProjectAgamemnon ships
# standalone example binaries, this directory can be updated to reference
# those instead.

cmake_minimum_required(VERSION 3.20)

message(STATUS "")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "  Workflow Examples — REMOVED (ADR-015)")
message(STATUS "  Agent hierarchy examples extracted to ProjectAgamemnon.")
message(STATUS "  No examples to build here.")
message(STATUS "═══════════════════════════════════════════════════════")
message(STATUS "")
