# OpenEPaperLink FW for Opticon Tags

<img src="https://github.com/user-attachments/assets/f81bcc18-4759-44eb-9bd8-7b931da35624" width="600">


## Disclaimer
OpenEPaperLink is an independent, open-source project and is not affiliated with, endorsed by, or otherwise connected to Opticon, its subsidiaries, or any of its products. The use of Opticon's brand name or related product names in this project is for informational purposes only. OpenEPaperLink is developed and maintained by the open-source community and is not officially supported by Opticon.

Please don't bother them with support questions, they can't help you with questions about this firmware.

The authors and contributors of OpenEPaperLink and the alternative firmware for Opticon tags cannot be held responsible for any damage, data loss, or other consequences that may result from the use of this alternative firmware. By using this firmware, you acknowledge that you do so at your own risk. OpenEPaperLink is provided "as-is," and no warranty, express or implied, is offered regarding its functionality or compatibility. It is a perpetual 'work-in-progress', and may very well never be finished at all!

## Introduction
This is an alternative firmware for a few of the Opticon Electronic Shelf labels. Currently, the following tags are supported:
* EE-214 BWRY
* EE-293RY
* EE-420R
* EE-750R

This firmware is 100% compatible with existing OpenEPaperLink accesspoints.

<img src="https://github.com/user-attachments/assets/5edd3521-38b8-46dc-bcb0-c1f802bce354" width="300">
<img src="https://github.com/user-attachments/assets/85532498-8cb9-436c-b724-5d301f5742c5" width="300">

## Specs
* CC2533 F96 SoC (8051 core, 32 Mhz, 6kB RAM, 96kbyte FLASH) 2.4GHz 802.15.4 radio
* BWR(Y) Display
* 1Mbyte/512Kb EEPROM

See the pages on the <a href="https://github.com/OpenEPaperLink/OpenEPaperLink/wiki/Opticon-2.1%E2%80%B3-BWRY-EE%E2%80%90214RY">OpenEPaperLink Wiki</a> for specific tag specs

## Compiling
This project has been tested only with SDCC 4.4.0. Your milage may vary using other compilers/versions. It uses a larger sized heap, to temporarily store all data for display and communication with the AP. This may confuse other SDCC versions.

Use the included Makefile to build the project, using ```make BUILD=Opticon_2.2_BWRY``` to build a version specific for a certain tag type

## Programming
CC2533 SoC's can be programmed with a bunch of different programmers, including DIY-programmers built with ESP32's. Another option for these SoC's is the CC-debugger. The tags have a 6-pin header with 1.27mm spacing. This allows commercially available test-clips to be used to temporarily connect to the PCB/SoC.

The firmware provides optional debug output on P2.0 at 115200 baud, you'll need a TTL->USB converter to read this data.

<img src="https://github.com/user-attachments/assets/aabe6766-79f0-4ec0-a8c0-308d3b1c2cdb" width="600">


<img src="https://github.com/user-attachments/assets/c5579af8-6d1e-4caf-ae72-866a3aa4f003" width="300">
<img src="https://github.com/user-attachments/assets/f7a3e38f-e950-4429-92d7-d4dc27736a18" width="300">
<img src="https://github.com/user-attachments/assets/c15a3162-011e-4261-a84f-98aeb09e1f44" width="300">


Please note that a stock tag come with a 'lock' on the SoC. It's only possible to erase the chip, not read it out. Flash the appropriate 'hex' file to the SoC, and you're done!

## Changelog

### v0029 - G5 compression ###
Many thanks to [@Larry Bank](https://github.com/bitbank2) for his awesome work making [compression](https://github.com/bitbank2/bb_epaper/blob/main/src/Group5.cpp) possible on such a limited target. Check out his many useful [repositories!](https://github.com/bitbank2?tab=repositories)

This version introduces optional G5 compression for image transfers. Depending on the image content, the image transfer size can be reduced substantially, especially when no dithering is used in the transferred image.

- G5 image compression now supported (datatypes 0x31 and 0x32)
- Block transfers are now shortened where possible, reducing battery consumption
- Fixed a bug where sometimes 7.5" tags would crash during image download
