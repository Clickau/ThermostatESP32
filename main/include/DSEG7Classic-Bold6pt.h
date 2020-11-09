#include <Arduino.h>
#include <Adafruit_GFX.h>

const uint8_t DSEG7Classic_Bold6pt7bBitmaps[] = {
  0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99,
  0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99,
  0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9,
  0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99,
  0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0x79, 0xE0, 0xC0, 0xF9, 0x99,
  0x99, 0x99, 0xF0, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0x81, 0x81, 0xC3, 0xC3,
  0xC3, 0xC3, 0x7E, 0xFF, 0x5F, 0xF0, 0x7E, 0x03, 0x03, 0x03, 0x03, 0x3D,
  0xBC, 0xC0, 0xC0, 0xC0, 0xC0, 0x7E, 0xFC, 0x0C, 0x18, 0x30, 0x6F, 0x5E,
  0x83, 0x06, 0x0C, 0x1F, 0xE0, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0x3D, 0x03,
  0x03, 0x03, 0x03, 0x7E, 0xC0, 0xC0, 0xC0, 0xC0, 0xBC, 0x3D, 0x03, 0x03,
  0x03, 0x03, 0x7E, 0x7E, 0xC0, 0xC0, 0xC0, 0xC0, 0xBC, 0xBD, 0xC3, 0xC3,
  0xC3, 0xC3, 0x7E, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0x81, 0x01, 0x03, 0x03,
  0x03, 0x03, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0xBD, 0xC3, 0xC3, 0xC3,
  0xC3, 0x7E, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0x3D, 0x03, 0x03, 0x03,
  0x03, 0x7E, 0xC0, 0x30, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99,
  0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0,
  0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0x7E, 0xC3,
  0xC3, 0xC3, 0xC3, 0xBD, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0xC0, 0xC0, 0xC0,
  0xC0, 0xBC, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0x3D, 0x7B, 0x06, 0x0C,
  0x18, 0x1F, 0x80, 0x03, 0x03, 0x03, 0x03, 0x3D, 0xBD, 0xC3, 0xC3, 0xC3,
  0xC3, 0x7E, 0x7F, 0x83, 0x06, 0x0C, 0x17, 0xAF, 0x60, 0xC1, 0x83, 0x03,
  0xF0, 0x7F, 0x83, 0x06, 0x0C, 0x17, 0xAF, 0x60, 0xC1, 0x83, 0x00, 0x7E,
  0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0x81, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0xC0,
  0xC0, 0xC0, 0xC0, 0xBC, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0x7F, 0xC0, 0x03,
  0x03, 0x03, 0x03, 0x01, 0x81, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0x7E, 0xC0,
  0xC0, 0xC0, 0xC0, 0xBC, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0xC1, 0x83, 0x06,
  0x08, 0x10, 0x30, 0x60, 0xC1, 0x81, 0xF8, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3,
  0x81, 0x81, 0xC3, 0xC3, 0xC3, 0xC3, 0x3C, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3,
  0x3C, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3,
  0xBD, 0xBC, 0xC0, 0xC0, 0xC0, 0xC0, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD,
  0x3D, 0x03, 0x03, 0x03, 0x03, 0x3D, 0x7B, 0x06, 0x0C, 0x18, 0x00, 0xC0,
  0xC0, 0xC0, 0xC0, 0xBC, 0x3D, 0x03, 0x03, 0x03, 0x03, 0x7E, 0xC1, 0x83,
  0x06, 0x0B, 0xD7, 0xB0, 0x60, 0xC1, 0x81, 0xF8, 0x81, 0xC3, 0xC3, 0xC3,
  0xC3, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0x81, 0x81, 0xC3, 0xC3, 0xC3, 0xC3,
  0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E,
  0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3,
  0xC3, 0xC3, 0xBD, 0x3D, 0x03, 0x03, 0x03, 0x03, 0x7E, 0x7E, 0x03, 0x03,
  0x03, 0x03, 0x01, 0x80, 0xC0, 0xC0, 0xC0, 0xC0, 0x7E, 0xF9, 0x99, 0x99,
  0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0,
  0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99,
  0x99, 0x99, 0xF0, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0xBD, 0xC3, 0xC3,
  0xC3, 0xC3, 0xC0, 0xC0, 0xC0, 0xC0, 0xBC, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3,
  0x7E, 0x3D, 0x7B, 0x06, 0x0C, 0x18, 0x1F, 0x80, 0x03, 0x03, 0x03, 0x03,
  0x3D, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0x7F, 0x83, 0x06, 0x0C, 0x17,
  0xAF, 0x60, 0xC1, 0x83, 0x03, 0xF0, 0x7F, 0x83, 0x06, 0x0C, 0x17, 0xAF,
  0x60, 0xC1, 0x83, 0x00, 0x7E, 0xC0, 0xC0, 0xC0, 0xC0, 0x80, 0x81, 0xC3,
  0xC3, 0xC3, 0xC3, 0x7E, 0xC0, 0xC0, 0xC0, 0xC0, 0xBC, 0xBD, 0xC3, 0xC3,
  0xC3, 0xC3, 0x7F, 0xC0, 0x03, 0x03, 0x03, 0x03, 0x01, 0x81, 0xC3, 0xC3,
  0xC3, 0xC3, 0x7E, 0x7E, 0xC0, 0xC0, 0xC0, 0xC0, 0xBC, 0xBD, 0xC3, 0xC3,
  0xC3, 0xC3, 0xC1, 0x83, 0x06, 0x08, 0x10, 0x30, 0x60, 0xC1, 0x81, 0xF8,
  0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0x81, 0x81, 0xC3, 0xC3, 0xC3, 0xC3, 0x3C,
  0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0x3C, 0xBD, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E,
  0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0xBC, 0xC0, 0xC0, 0xC0, 0xC0, 0x7E,
  0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0x3D, 0x03, 0x03, 0x03, 0x03, 0x3D, 0x7B,
  0x06, 0x0C, 0x18, 0x00, 0xC0, 0xC0, 0xC0, 0xC0, 0xBC, 0x3D, 0x03, 0x03,
  0x03, 0x03, 0x7E, 0xC1, 0x83, 0x06, 0x0B, 0xD7, 0xB0, 0x60, 0xC1, 0x81,
  0xF8, 0x81, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0x81,
  0x81, 0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0xBD,
  0xC3, 0xC3, 0xC3, 0xC3, 0x7E, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0xBD, 0xC3,
  0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xC3, 0xBD, 0x3D, 0x03, 0x03, 0x03,
  0x03, 0x7E, 0x7E, 0x03, 0x03, 0x03, 0x03, 0x01, 0x80, 0xC0, 0xC0, 0xC0,
  0xC0, 0x7E, 0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0,
  0xF9, 0x99, 0x99, 0x99, 0xF0, 0xF9, 0x99, 0x99, 0x99, 0xF0 };

const GFXglyph DSEG7Classic_Bold6pt7bGlyphs[] = {
  {     0,   0,   0,   2,    0,    1 },   // 0x20 ' '
  {     0,   0,   0,  10,    0,    1 },   // 0x21 '!'
  {     0,   4,   9,   4,    0,   -8 },   // 0x22 '"'
  {     5,   4,   9,   4,    0,   -8 },   // 0x23 '#'
  {    10,   4,   9,   4,    0,   -8 },   // 0x24 '$'
  {    15,   4,   9,   4,    0,   -8 },   // 0x25 '%'
  {    20,   4,   9,   4,    0,   -8 },   // 0x26 '&'
  {    25,   4,   9,   4,    0,   -8 },   // 0x27 '''
  {    30,   4,   9,   4,    0,   -8 },   // 0x28 '('
  {    35,   4,   9,   4,    0,   -8 },   // 0x29 ')'
  {    40,   4,   9,   4,    0,   -8 },   // 0x2A '*'
  {    45,   4,   9,   4,    0,   -8 },   // 0x2B '+'
  {    50,   4,   9,   4,    0,   -8 },   // 0x2C ','
  {    55,   6,   2,  10,    2,   -6 },   // 0x2D '-'
  {    57,   2,   1,   0,   -1,    0 },   // 0x2E '.'
  {    58,   4,   9,   4,    0,   -8 },   // 0x2F '/'
  {    63,   8,  12,  10,    1,  -11 },   // 0x30 '0'
  {    75,   2,  10,  10,    7,  -10 },   // 0x31 '1'
  {    78,   8,  12,  10,    1,  -11 },   // 0x32 '2'
  {    90,   7,  12,  10,    2,  -11 },   // 0x33 '3'
  {   101,   8,  10,  10,    1,  -10 },   // 0x34 '4'
  {   111,   8,  12,  10,    1,  -11 },   // 0x35 '5'
  {   123,   8,  12,  10,    1,  -11 },   // 0x36 '6'
  {   135,   8,  11,  10,    1,  -11 },   // 0x37 '7'
  {   146,   8,  12,  10,    1,  -11 },   // 0x38 '8'
  {   158,   8,  12,  10,    1,  -11 },   // 0x39 '9'
  {   170,   2,   6,   2,    0,   -8 },   // 0x3A ':'
  {   172,   4,   9,   4,    0,   -8 },   // 0x3B ';'
  {   177,   4,   9,   4,    0,   -8 },   // 0x3C '<'
  {   182,   4,   9,   4,    0,   -8 },   // 0x3D '='
  {   187,   4,   9,   4,    0,   -8 },   // 0x3E '>'
  {   192,   4,   9,   4,    0,   -8 },   // 0x3F '?'
  {   197,   4,   9,   4,    0,   -8 },   // 0x40 '@'
  {   202,   8,  11,  10,    1,  -11 },   // 0x41 'A'
  {   213,   8,  11,  10,    1,  -10 },   // 0x42 'B'
  {   224,   7,   7,  10,    1,   -6 },   // 0x43 'C'
  {   231,   8,  11,  10,    1,  -10 },   // 0x44 'D'
  {   242,   7,  12,  10,    1,  -11 },   // 0x45 'E'
  {   253,   7,  11,  10,    1,  -11 },   // 0x46 'F'
  {   263,   8,  12,  10,    1,  -11 },   // 0x47 'G'
  {   275,   8,  10,  10,    1,  -10 },   // 0x48 'H'
  {   285,   2,   5,  10,    7,   -5 },   // 0x49 'I'
  {   287,   8,  11,  10,    1,  -10 },   // 0x4A 'J'
  {   298,   8,  11,  10,    1,  -11 },   // 0x4B 'K'
  {   309,   7,  11,  10,    1,  -10 },   // 0x4C 'L'
  {   319,   8,  11,  10,    1,  -11 },   // 0x4D 'M'
  {   330,   8,   6,  10,    1,   -6 },   // 0x4E 'N'
  {   336,   8,   7,  10,    1,   -6 },   // 0x4F 'O'
  {   343,   8,  11,  10,    1,  -11 },   // 0x50 'P'
  {   354,   8,  11,  10,    1,  -11 },   // 0x51 'Q'
  {   365,   7,   6,  10,    1,   -6 },   // 0x52 'R'
  {   371,   8,  11,  10,    1,  -10 },   // 0x53 'S'
  {   382,   7,  11,  10,    1,  -10 },   // 0x54 'T'
  {   392,   8,   6,  10,    1,   -5 },   // 0x55 'U'
  {   398,   8,  11,  10,    1,  -10 },   // 0x56 'V'
  {   409,   8,  11,  10,    1,  -10 },   // 0x57 'W'
  {   420,   8,  10,  10,    1,  -10 },   // 0x58 'X'
  {   430,   8,  11,  10,    1,  -10 },   // 0x59 'Y'
  {   441,   8,  12,  10,    1,  -11 },   // 0x5A 'Z'
  {   453,   4,   9,   4,    0,   -8 },   // 0x5B '['
  {   458,   4,   9,   4,    0,   -8 },   // 0x5C '\'
  {   463,   4,   9,   4,    0,   -8 },   // 0x5D ']'
  {   468,   4,   9,   4,    0,   -8 },   // 0x5E '^'
  {   473,   4,   9,   4,    0,   -8 },   // 0x5F '_'
  {   478,   4,   9,   4,    0,   -8 },   // 0x60 '`'
  {   483,   8,  11,  10,    1,  -11 },   // 0x61 'a'
  {   494,   8,  11,  10,    1,  -10 },   // 0x62 'b'
  {   505,   7,   7,  10,    1,   -6 },   // 0x63 'c'
  {   512,   8,  11,  10,    1,  -10 },   // 0x64 'd'
  {   523,   7,  12,  10,    1,  -11 },   // 0x65 'e'
  {   534,   7,  11,  10,    1,  -11 },   // 0x66 'f'
  {   544,   8,  12,  10,    1,  -11 },   // 0x67 'g'
  {   556,   8,  10,  10,    1,  -10 },   // 0x68 'h'
  {   566,   2,   5,  10,    7,   -5 },   // 0x69 'i'
  {   568,   8,  11,  10,    1,  -10 },   // 0x6A 'j'
  {   579,   8,  11,  10,    1,  -11 },   // 0x6B 'k'
  {   590,   7,  11,  10,    1,  -10 },   // 0x6C 'l'
  {   600,   8,  11,  10,    1,  -11 },   // 0x6D 'm'
  {   611,   8,   6,  10,    1,   -6 },   // 0x6E 'n'
  {   617,   8,   7,  10,    1,   -6 },   // 0x6F 'o'
  {   624,   8,  11,  10,    1,  -11 },   // 0x70 'p'
  {   635,   8,  11,  10,    1,  -11 },   // 0x71 'q'
  {   646,   7,   6,  10,    1,   -6 },   // 0x72 'r'
  {   652,   8,  11,  10,    1,  -10 },   // 0x73 's'
  {   663,   7,  11,  10,    1,  -10 },   // 0x74 't'
  {   673,   8,   6,  10,    1,   -5 },   // 0x75 'u'
  {   679,   8,  11,  10,    1,  -10 },   // 0x76 'v'
  {   690,   8,  11,  10,    1,  -10 },   // 0x77 'w'
  {   701,   8,  10,  10,    1,  -10 },   // 0x78 'x'
  {   711,   8,  11,  10,    1,  -10 },   // 0x79 'y'
  {   722,   8,  12,  10,    1,  -11 },   // 0x7A 'z'
  {   734,   4,   9,   4,    0,   -8 },   // 0x7B '{'
  {   739,   4,   9,   4,    0,   -8 },   // 0x7C '|'
  {   744,   4,   9,   4,    0,   -8 },   // 0x7D '}'
  {   749,   4,   9,   4,    0,   -8 } }; // 0x7E '~'

const GFXfont DSEG7Classic_Bold6pt7b = {
  (uint8_t  *)DSEG7Classic_Bold6pt7bBitmaps,
  (GFXglyph *)DSEG7Classic_Bold6pt7bGlyphs,
  0x20, 0x7E, 13 };

// Approx. 1426 bytes