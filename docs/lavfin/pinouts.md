# LAFVIN ESP32-S3 Multimedia Kit: carrier pinout

Transcribed from the silkscreen table on the back of the carrier board (photo `IMG_4706.jpeg`
in `~/Downloads/lavfin/`, kept out of the repo because the firmware's asset packer embeds every
image it finds). The rows follow the silkscreen top to bottom, left column then right column,
which is the physical order of the dev-board socket. Every assignment agrees with the vendor's
example code.

| Left pin | Left function | Right pin | Right function |
| -------- | ------------- | --------- | -------------- |
| 3V3      |               | GPIO43    | Serial_TX      |
| EN       | RST           | GPIO44    | Serial_RX      |
| GPIO4    | Cam_SIOD      | GPIO1     | IIC_SCL        |
| GPIO5    | Cam_SIOC      | GPIO2     | IIC_SDA        |
| GPIO6    | Cam_VSYNC     | GPIO42    | IIS_BCK        |
| GPIO7    | Cam_HREF      | GPIO41    | IIS_DIN        |
| GPIO15   | Cam_XCLK      | GPIO40    | SD_DATA        |
| GPIO16   | Cam_Y9        | GPIO39    | SD_CLK         |
| GPIO17   | Cam_Y8        | GPIO38    | SD_CMD         |
| GPIO18   | Cam_Y7        | GPIO37    | PSRAM          |
| GPIO8    | Cam_Y4        | GPIO36    | PSRAM          |
| GPIO3    | Strapping     | GPIO35    | PSRAM          |
| GPIO46   | Strapping     | GPIO0     | TFT_DC         |
| GPIO9    | Cam_Y3        | GPIO45    | Strapping      |
| GPIO10   | Cam_Y5        | GPIO48    | WS2812         |
| GPIO11   | Cam_Y2        | GPIO47    | TFT_CS         |
| GPIO12   | Cam_Y6        | GPIO21    | TFT_SCK        |
| GPIO13   | Cam_PCLK      | GPIO20    | TFT_MOSI       |
| GPIO14   | IIS_LCK       | GPIO19    | ADC_BAT        |
| 5V       |               | GND       |                |

Reading notes:

- The silkscreen prints `Cam_HERF` for the camera's HREF line and `Stropping` once for
  `Strapping`; both are corrected above.
- Camera data lines map to the sensor as Y2 to Y9 = D0 to D7, so D0 is GPIO11, D1 GPIO9, D2
  GPIO8, D3 GPIO10, D4 GPIO12, D5 GPIO18, D6 GPIO17, D7 GPIO16.
- `IIS_DIN` is the audio module's data input, driven by the ESP32-S3; `IIS_BCK` and `IIS_LCK`
  are its bit and word clocks. The module is a PCM5100 DAC with a PAM8403 amplifier, with no
  I2C.
- `IIC` carries the FT6336U touch controller and the MAX3010x heart-rate sensor.
- GPIO19 and GPIO20 are the ESP32-S3's native USB pins (D- and D+). On this carrier they are
  the battery divider and the display's MOSI, which is why the dev board's OTG port cannot host
  a USB interface until those two pins are lifted out of the socket. GPIO0 is both the BOOT
  button, the pedal's footswitch, and the display's D/C line. The plan for all three is in
  `NOTES.md`.
- Free pins with the camera ribbon unplugged: GPIO3 and GPIO46 always; GPIO4 to GPIO13 and
  GPIO15 to GPIO18 then become free as well.
