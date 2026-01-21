# keyer_cwnet - CWNet protocol implementation
#
# Provides timestamp encoding/decoding, frame parsing, PING handling,
# and TCP client for the CW streaming protocol.

idf_component_register(
    SRCS
        "src/cwnet_timestamp.c"
        "src/cwnet_frame.c"
        "src/cwnet_ping.c"
        "src/cwnet_client.c"
        "src/cwnet_socket.c"
    INCLUDE_DIRS "include"
    REQUIRES
        keyer_config
        keyer_logging
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
