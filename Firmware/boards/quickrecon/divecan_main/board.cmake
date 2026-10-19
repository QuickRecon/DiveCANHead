board_runner_args(openocd
    --cmd-pre-init "source [find interface/stlink.cfg]"
    --cmd-pre-init "source [find target/stm32l4x.cfg]"
    --cmd-pre-init "reset_config srst_only srst_nogate connect_assert_srst"
)
board_runner_args(stm32cubeprogrammer --port=swd)

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/stm32cubeprogrammer.board.cmake)
