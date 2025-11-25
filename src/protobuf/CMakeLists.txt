find_package(Protobuf REQUIRED)

set(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../windowmanager.proto")

set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}")

set(PROTO_SRCS "${PROTO_GEN_DIR}/windowmanager.pb.cc")
set(PROTO_HDRS "${PROTO_GEN_DIR}/windowmanager.pb.h")

add_custom_command(
  OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
  COMMAND ${PROTOBUF_PROTOC_EXECUTABLE}
  ARGS --cpp_out=${PROTO_GEN_DIR}
       --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/../
       ${PROTO_FILE}
  DEPENDS ${PROTO_FILE}
  COMMENT "Generating protobuf C++ files from windowmanager.proto"
  VERBATIM
)

add_library(windowmanager_proto ${PROTO_SRCS} ${PROTO_HDRS})
target_link_libraries(windowmanager_proto PUBLIC protobuf::libprotobuf)
target_include_directories(windowmanager_proto PUBLIC ${PROTO_GEN_DIR})

# Make generated headers available
set_target_properties(windowmanager_proto PROPERTIES
  PUBLIC_HEADER ${PROTO_HDRS}
)
