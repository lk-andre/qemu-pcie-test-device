set(KERNEL_MODULE_NAME "pcie-test-module")

set(KERNEL_SRC ${KERNEL_MODULE_NAME}.c)
set(KERNEL_OUTPUT ${KERNEL_MODULE_NAME}.ko)

if(NOT DEFINED KERNEL_HEADER_DIR)
  message(
    FATAL_ERROR
      "ERROR: KERNEL_HEADER_DIR is not defined. Set the var by -D KERNEL_HEADER_DIR=<path> in your CMake command.")
endif()

# Generate the Kbuild file to build using the template
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/Kbuild.in ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild @ONLY)

add_custom_target(${KERNEL_MODULE_NAME} ALL DEPENDS ${KERNEL_OUTPUT})

add_custom_command(
  OUTPUT ${KERNEL_OUTPUT}
  # Using $(src) custom rule
  # (https://github.com/torvalds/linux/blob/master/Documentation/kbuild/makefiles.rst#custom-rules)
  COMMAND $(MAKE) -C ${KERNEL_HEADER_DIR} M=${CMAKE_CURRENT_BINARY_DIR} src=${CMAKE_CURRENT_SOURCE_DIR} modules
  DEPENDS ${KERNEL_SRC} ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running Kernel Build"
  VERBATIM)
