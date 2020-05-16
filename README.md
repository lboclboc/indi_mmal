# indi_mmal
indi-driver for mmal camera on RPi

Just started hacking.

References
----------
- [MMAL Camera API](http://www.jvcref.com/files/PI/documentation/html/)
- [IMX477 Product Information](https://www.sony-semicon.co.jp/products/common/pdf/IMX477-AACK_Flyer.pdf)
- [INDI Development Environment](https://indilib.org/develop/developer-manual/163-setting-development-environment.html)



Notes
-----
When getting a raw image (jpeg+raw), the first part of the file is a normal jpeg, in the remaining part (18711040 bytes) then
comes a 32768 bytes header starting with the text "BRCMo".
The raw image then follows which 18678272 bytes with rows 6112 bytes long: 6084 bytes (4056*1.5!!) of pixels (groups of 3),  12 blanking?? and 16 image related data.
In total 3056 rows with the last 16 rows containing other data (even in the last 28 bytes of the rows).

Pixels are ordered by | Gm | Rm | Gl+Rl |
                      | Bm | Gm | Bl+Gl |
m : MSB 8 bits, l = LSB 4 bits
