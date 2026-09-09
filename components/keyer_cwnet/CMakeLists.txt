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
        "src/cwnet_feed.c"
        "src/cwnet_socket.c"
        # src/cwnet_play.c is deliberately absent: the station daemon's
        # playback engine is host-only (KTD2). It has no place on the box,
        # whose RT path never plays a remote client's keying.
    INCLUDE_DIRS "include"
    REQUIRES
        keyer_core
        keyer_config
        keyer_logging
        esp_timer
        lwip
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wconversion
    -Wshadow
    -Wstrict-prototypes
)
