# TM1640B driver for the CAI-274 RGB LED clock kit: hard colours per segment, steady; see the README.
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.const import CONF_CLK_PIN, CONF_DIO_PIN, CONF_ID

CONF_HALF_BIT_US = "half_bit_us"

tm1640rgb_ns = cg.esphome_ns.namespace("tm1640rgb")
TM1640RGB = tm1640rgb_ns.class_("TM1640RGB", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(TM1640RGB),
        cv.Required(CONF_CLK_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_DIO_PIN): pins.internal_gpio_output_pin_number,
        # TM1640B: SCLK max 1 MHz, pulse >= 400 ns (datasheet p3-4); 2 us = 125 kHz bit rate.
        cv.Optional(CONF_HALF_BIT_US, default=2): cv.int_range(1, 50),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_pins(config[CONF_CLK_PIN], config[CONF_DIO_PIN]))
    cg.add(var.set_half_bit_us(config[CONF_HALF_BIT_US]))
