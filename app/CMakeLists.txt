set(CMAKE_C_STANDARD 11)

add_executable(firmware
	backlight.c
	debug.c
	fifo.c
	gpioexp.c
	puppet_i2c.c
	interrupt.c
	keyboard.c
	main.c
	reg.c
	touchpad.c
	usb.c
	usb_descriptors.c
	pi.c
	rtc.c
	update.c
)

add_compile_options(-Wall -Wextra -Wpedantic)

target_include_directories(firmware PRIVATE ${CMAKE_CURRENT_LIST_DIR})

target_link_libraries(firmware
	cmsis_core
	hardware_i2c
	hardware_pwm
	hardware_adc
	hardware_rtc
	hardware_flash
	pico_bootsel_via_double_reset
	pico_stdlib
	tinyusb_device
)

# create map/bin/hex/uf2 file in addition to elf
pico_add_extra_outputs(firmware)

# printf targets
pico_enable_stdio_uart(firmware 1)

pico_add_uf2_output(firmware)
pico_add_hex_output(firmware)

# binary info in flash
pico_set_program_name(firmware "I2C Puppet")
pico_set_program_version(firmware "3.0")

# printf targets
pico_enable_stdio_uart(firmware 1)

