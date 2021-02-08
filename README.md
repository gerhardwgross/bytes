# bytes
Usage:  bytes [-xUecwl] fileneme [start] [end]<br>
Prints the ASCII char values as unsigned ints (0-255) of each character<br>
in the specified file and their character representation, if printable.<br>
<br>
 o Works identically on all file types - text and binary.<br>
 o [start] is the char # in filename at which to begin displaying.<br>
 o [end] is the char # in filename at which to end displaying.<br>
 o If start and end are not supplied, all bytes are displayed.<br>
 o If start is supplied and end is not supplied, the display<br>
   o begins at the start char # and continues until EOF.<br>
   o It is generally convenient to pipe the output of this utility to \"more\" (e.g \"bytes tmp.exe | more\").<br>
     <br>
 -x prints ASCII values in hexadecimal (default is decimal)<br>
 -U assumes text file written in Unicode (2 octets per char)<br>
 -e report the endianness of current machine then exit<br>
 -c compute and print the 32 bit CRC of the contents of the specified file<br>
 -wXX Set width of output (number of chars across screen = XX)<br>
 -lX Set num lines of output to print, when no 'end' value is specified. X must be an integer.
