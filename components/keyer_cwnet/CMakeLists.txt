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
        # src/cwnet_play.c and src/cwnet_server.c are deliberately absent:
        # the station daemon's playback engine and its server core are
        # host-only (KTD2). They have no place on the box, whose RT path
        # never plays a remote client's keying and which is never the
        # server. They are built and tested by test_host/ alone.
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
