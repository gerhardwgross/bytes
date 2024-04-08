/****************************************************************************
// bytes -- utility program written by Gerhard Gross (PSU '95)
//
// Copyright (c) 1995 - 018 Gerhard W. Gross.
//
// THIS SOFTWARE IS PROVIDED BY GERHARD W. GROSS ``AS IS'' AND ANY
// EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL GERHARD W. GROSS 
// BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
// NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
// HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
// OF THE POSSIBILITY OF SUCH DAMAGE.
//
// PERMISSION TO USE, COPY, MODIFY, AND DISTRIBUTE THIS SOFTWARE AND ITS
// DOCUMENTATION FOR ANY PURPOSE AND WITHOUT FEE IS HEREBY GRANTED,
// PROVIDED THAT THE ABOVE COPYRIGHT NOTICE, THE ABOVE DISCLAIMER
// NOTICE, THIS PERMISSION NOTICE, AND THE FOLLOWING ATTRIBUTION
// NOTICE APPEAR IN ALL SOURCE CODE FILES AND IN SUPPORTING
// DOCUMENTATION AND THAT GERHARD W. GROSS BE GIVEN ATTRIBUTION
// AS THE MAIN AUTHOR OF THIS PROGRAM IN THE FORM OF A TEXTUAL
// MESSAGE AT PROGRAM STARTUP OR IN THE DISPLAY OF A USAGE MESSAGE,
// OR IN DOCUMENTATION (ONLINE OR TEXTUAL) PROVIDED WITH THIS PROGRAM.
//
// ALL OR PARTS OF THIS CODE WERE WRITTEN BY GERHARD W. GROSS, 1995-1999.
//
****************************************************************************/

#ifdef _WIN32
#include<windows.h>
#endif

#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <fstream>
#include <climits>

#ifdef BYTES_LITTLE_ENDIAN
#undef BYTES_LITTLE_ENDIAN
#endif
#define BYTES_LITTLE_ENDIAN     1

#ifdef BYTES_BIG_ENDIAN
#undef BYTES_BIG_ENDIAN
#endif
#define BYTES_BIG_ENDIAN        2

#define BYTES_DEF_NUM_COLS      20
#define BYTES_DEF_NUM_LINES     15

#define BYTES_DECIMAL_FORMAT    0
#define BYTES_HEX_FORMAT        1
`
#define BYTES_EIGHT_BIT_ASCII   0
#define BYTES_UNICODE           1

static int g_endianness;
static int g_numberFormat       = BYTES_DECIMAL_FORMAT;
static int g_textEncoding       = BYTES_EIGHT_BIT_ASCII;
static int g_numColWidth        = BYTES_DEF_NUM_COLS;
static long long g_linesToPrint   = BYTES_DEF_NUM_LINES;
static bool g_reportEndianness  = false;
static bool g_compute16BitCRC   = false;
static bool g_compute32BitCRC   = false;

/* Table of CRCs of all 8-bit messages. */
unsigned long g_crc16Table[256];
unsigned long g_crc32Table[256];

/* Flag: has the table been computed? Initially false. */
static bool g_is16BitCRCTableComputed = false;
static bool g_is32BitCRCTableComputed = false;

using namespace std;

/***************************************************************************
Create an exception class to throw
***************************************************************************/
class Exception
{
public:
    char msg[1024];
    Exception() { msg[0] = 0; }
    Exception(const char* str)
    {
#ifdef _WIN32
        strcpy_s(msg, str);
#else
        strncpy(msg, str, strlen(str));
#endif
    }
};

void HandleFileIOErr(char* filePath);

#ifndef _WIN32

#define _stat64 stat64

char* strerror_s(char* buff, int buffSz, int errNum)
{
   buff[0] = 0;
   char* msg = strerror(errno);
   int msgSz = strlen(msg);
   if (msgSz < buffSz)
       strcpy(buff, msg);
   return buff;
}
#endif

/***************************************************************************
   Determine if this is a big or little endian machine. Set an unsigned
   int (4 bytes or so) equal to 1. memcpy the first char at the address
   of the 4 char int into an unsigned char. On little endian machines that
   char will be the low order char (little end) which equals 1, and on big
   endian machines it will be the high order char (big end) which equals 0.
***************************************************************************/

int DetectEndianness()
{
    unsigned char uChar;
    unsigned int uInt = 1;

    memcpy(&uChar, &uInt, 1);

    return uChar == 1 ? BYTES_LITTLE_ENDIAN : BYTES_BIG_ENDIAN;
}

/***************************************************************************
   Print on screen the endianness of this machine.
***************************************************************************/

void ReportEndianness()
{
    if (g_endianness == BYTES_LITTLE_ENDIAN)
    {
        printf("\n    This machine uses Little Endian addressing.\n");
        printf("    - Memory addresses point to the lowest order char of multibyte variables.\n\n");
    }
    else
    {
        printf("\n    This machine uses Big Endian addressing.\n");
        printf("    - Memory addresses point to the highest order char of multibyte variables.\n\n");
    }
}

/***************************************************************************
   Print the application usage to screen.
***************************************************************************/

void PrintUsage()
{
    fprintf(stderr,
        "\n\
         Usage:  bytes [-xUecwl] fileneme [start] [end]\n\n\
         Prints the ASCII char values as unsigned ints (0-255) of each character\n\
         in the specified file and their character representation, if printable.\n\
         o Works identically on all file types - text and binary.\n\
         o [start] is the char # in filename at which to begin displaying.\n\
         o [end] is the char # in filename at which to end displaying.\n\
         o If start and end are not supplied, all bytes are displayed.\n\
         o If start is supplied and end is not supplied, the display\n\
           begins at the start char # and continues until EOF.\n\
         o It is generally convenient to pipe the output of this utility\n\
           to \"more\" (e.g \"bytes tmp.exe | more\").\n\
         \n\
         -x prints ASCII values in hexadecimal (default is decimal)\n\
         -U assumes text file written in Unicode (2 octets per char)\n\
         -e report the endianness of current machine then exit\n\
         -c compute and print the 32 bit CRC of the contents of the specified file\n\
         -wXX Set width of output (number of chars across screen = XX)\n\
         -lX Set num lines of output to print, when no 'end' value is\n\
             specified. X must be an integer.\n\
         \n\
         ****** Gerhard W. Gross ******\n\n");
}

/***************************************************************************
   This function removes the second character from the passed string.  This
   is called from deal_with_options() to remove the special purpose slash
   character.
***************************************************************************/

void shift(char *str)
{
   int i, len;
   len = strlen(str);

   for(i = 1; i < len; i++)
      str[i] = str[i + 1];
}

/***************************************************************************
   This function checks all args to see if they begin with a hyphen.
   If so the necessary flags are set.  argc and argv[] are adjusted
   accordingly, set back to the condition of the option not having been
   supplied at the com line (i.e. all except the first argv[] ptr are
   bumped back in the array).
***************************************************************************/

int deal_with_options(int arrgc, char *arrgv[])
{
    long i, j, num_opts;

    for(j = 1; j < arrgc; j++)
    {
        // If encounter a pipe symbol, '|', args handled by this app are done
        if(*arrgv[j] == '|')
            arrgc = j; // Set num args for this app to current loop counter value

        // Only process as a switch or option if begins with '-'
        if(*arrgv[j] == '-')
        {
            if(*(arrgv[j] + 1) == '/')
            {
                // Remove the backslash for option processing then go on to
                // next command line arg. Succeeding with '/' signals that
                // the hyphen is not to be interpreted as a cmd line option.
                shift(arrgv[j]);
            }
            else
            {
                num_opts = strlen(arrgv[j]) - 1;
                for(i = 1; i <= num_opts; i++)
                {
                    switch(*(arrgv[j] + i))
                    {
                        case 'x':
                            g_numberFormat = BYTES_HEX_FORMAT;
                            break;
                        case 'U':
                            g_textEncoding = BYTES_UNICODE;
                            break;
                        case 'e':
                            g_reportEndianness = true;
                            ReportEndianness();
                            // Set return var to signan caller to exit app
                            break;
                        case 'c':
                            g_compute32BitCRC = true;
                            break;
                        //case 'C':
                        //    g_compute16BitCRC = true;
                        //    break;
                        case 'w':
                            // This option must be succeeded with an integer and
                            // then a space. Convert string integer to numeric value.
                            g_numColWidth = atol(arrgv[j] + i + 1);

                            // Error check
                            if (g_numColWidth < 1)
                                g_numColWidth = BYTES_DEF_NUM_COLS;

                            // Now move loop counter to end of this option
                            i = num_opts;
                            break;
                        case 'l':
                            // This option must be succeeded with an integer and
                            // then a space. Convert string integer to numeric value.
                            g_linesToPrint = strtoll(arrgv[j] + i + 1, NULL, 0);

                            // Error check
                            if (g_linesToPrint < 1)
                                g_linesToPrint = BYTES_DEF_NUM_LINES;

                            // Now move loop counter to end of this option
                            i = num_opts;
                            break;
                        default:
                            fprintf(stderr,"Invalid option");
                            break;
                    }
                }

                for(i = j; i < arrgc - 1; i++)
                    arrgv[i] = arrgv[i + 1];

                arrgc--;
                j--;
            }
        }
    }

    return arrgc;
}

// Used only by Make32BitCRCTable().
unsigned long Reflect(unsigned long ref, char ch)
{
    unsigned long value(0);

    // Swap bit 0 for bit 7
    // bit 1 for bit 6, etc.
    for(int i = 1; i < (ch + 1); i++)
    {
        if(ref & 1)
            value |= 1 << (ch - i);
        ref >>= 1;
    }
    return value;
} 

// Call this function only once to initialize the CRC table.
void Make32BitCRCTable()
{
    // This is the official polynomial used by CRC-32
    // in PKZip, WinZip and Ethernet.
    unsigned long ulPolynomial = 0x04c11db7;

    // 256 values representing ASCII character codes.
    for(int i = 0; i <= 0xFF; i++)
    {
        g_crc32Table[i] = Reflect(i, 8) << 24;
        for (int j = 0; j < 8; j++)
                g_crc32Table[i] = (g_crc32Table[i] << 1) ^ (g_crc32Table[i] & (1 << 31) ? ulPolynomial : 0);
        g_crc32Table[i] = Reflect(g_crc32Table[i], 32);
    }

    g_is32BitCRCTableComputed = true;
}

//***************************************************************************/
// Once the lookup table has been filled in by the two functions above,
// this function creates all CRCs using only the lookup table.
// Be sure to use unsigned variables,
// because negative values introduce high bits
// where zero bits are required.
//***************************************************************************/

unsigned long Compute32BitCRC( char* filePath, long long fileSz)
{
    // Start out with all bits set high.
    unsigned long ulCRC = 0xffffffff;
    unsigned long maxBlockSz = 20000000;
    unsigned long bytesToRead, crcVal = 0;
    std::streamsize bytesRead;
    long long bytesRemaining;
    //int flDescIn = 0;
    errno = 0;
    char* ptr;
    char* buff;
    ifstream inFile;

    try
    {
        if (!g_is32BitCRCTableComputed)
            Make32BitCRCTable();

        printf("\n  File size: %lld\n", fileSz);

        // Allocate memory for the temp buffer
        ptr = new char[fileSz < (long long)maxBlockSz ? fileSz : maxBlockSz];

        // Must run CRC computation below on unsigned integral type so convert pointer
        // from char (signed) to unsigned char here.
        buff = ptr;

        inFile.open(filePath, ios::in | ios::binary);
        if (!inFile.good())
        {
            HandleFileIOErr(filePath);
            throw Exception();
        }

        printf("  ");
        bytesRemaining = fileSz;
        while (bytesRemaining > 0)
        {
            // Get block size or num left, read from infile, write to outfile
            bytesToRead     = (unsigned long) (bytesRemaining < maxBlockSz ? bytesRemaining : maxBlockSz);
            inFile.read(buff, bytesToRead);
            bytesRead = inFile.gcount();
            bytesRemaining  -= bytesRead;
            //printf("    Copied %ld bytes to memory buffer, bytes remaining in this file: %lld\n", bytesRead, bytesRemaining);
            printf(".");

            // Perform the CRC algorithm on each character in the string, using the lookup table values.
            for (unsigned long i = 0; i < bytesRead; ++i)
                ulCRC = (ulCRC >> 8) ^ g_crc32Table[(ulCRC & 0xFF) ^ buff[i]];
        }

        // Exclusive OR the result with the beginning value.
        crcVal = ulCRC ^ 0xffffffff;
    }
    catch (Exception)
    {
        crcVal = 0;
    }

    inFile.close();

    return crcVal;
} 


/***************************************************************************
Get the width, in characters, of the console window in which this program
is running.
***************************************************************************/
int GetConsoleWidth()
{
#ifdef _WIN32

    CONSOLE_SCREEN_BUFFER_INFO csbi;
    int columns, rows, retCols;

    GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
    columns = csbi.srWindow.Right - csbi.srWindow.Left + 1;
    rows = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;

    retCols = (columns - 1) / 4;
    return ((retCols) < 5 ? 40 : retCols);
#else
    return 20;
#endif
}

/***************************************************************************
   MAIN
***************************************************************************/

int main(int argc, char *argv[])
{
    unsigned char uchr;
    char* buff;
    int j, retVal = 0;
    long long start, end, fileSz, fl_pos, chunkSz, linesPrinted;
    unsigned int crc, tmp, defNumToShow;
    int fileRet = 0;
    struct _stat64 statBuff;
    errno = 0;
    ifstream inFile;

    try
    {

        g_numColWidth = GetConsoleWidth();
        g_endianness = DetectEndianness();

        argc = deal_with_options(argc, argv);

        if(argc != 2 && argc != 3 && argc != 4)
        {
            if (!g_reportEndianness && !g_compute32BitCRC && !g_compute16BitCRC)
                PrintUsage(); // Don't need to print usage in these cases
            throw Exception();
        }

        if (_stat64(argv[1], &statBuff) != 0)
        {
            HandleFileIOErr(argv[1]);
            throw Exception();
        }

        fileSz = statBuff.st_size;

        if (g_compute16BitCRC || g_compute32BitCRC)
        {
    //        if (g_compute16BitCRC)
    //            printf("CRC16 = %x\n", (crc = Compute16BitCRC(argv[1])));

            if (g_compute32BitCRC)
            {
                crc = Compute32BitCRC(argv[1], fileSz);
                printf("\n  CRC32 = %x\n", crc);
            }
        }

        if (g_compute32BitCRC && argc < 3)
            throw Exception(); // No error, just finished

        // By default, if no start and/or end byte numbers are given on the command line,
        // show about a page of bytes.
        start = 0;
        end = (defNumToShow = BYTES_DEF_NUM_COLS * BYTES_DEF_NUM_LINES) < fileSz ? defNumToShow : fileSz;

        if(argc >= 3)
        {
            start = strtoll(argv[2], NULL, 0) - 1;     /* char # to start reading. */
            end = start + defNumToShow;  /* set default end in case not given on cmd line */
            if(start < 1 || start >= fileSz)
                start = 0;
        }

        if(argc == 4)
        {
            end = strtoll(argv[3], NULL, 0);           /* char # to stop reading. */
            if(end < 1 || end > fileSz)
                end = fileSz;

            // Do not limit the number of lines to be printed
            g_linesToPrint = INT_MAX;
        }
        printf("start at char #%d, end at char #%d\n", start, end);

        if (end <= start)
        {
            if (end < start)
                printf("\n  end byte location (%lld) is less than start byte location (%lld).", end, start);
            throw Exception();
        }

        // Open the file 
        inFile.open(argv[1], ios::in | ios::binary);
        if(!inFile)
        {
            printf("\nError opening %s! (err: %d). Does it exist (in this directory)?\n", argv[1], fileRet);
            inFile.clear();
            return 1;
        }

        /****************************************************************************
        The following code block prints the output across the screen horizontally.
        For Intel machines I had to copy the character into an int variable to
        get it to print out as a number, in the second for loop.  This forced me
        to define some variables to distinguish between little and big endian char
        orders. (I'm only copying one char into a 4 byte integer.)
        ****************************************************************************/

        fl_pos          = start;
        linesPrinted    = 0;
        buff            = (char*)malloc(g_numColWidth);

        printf("\n");

        // If not starting at the beginning of the file, set the file read cursor to the start byte
        if (start != 0)
            inFile.seekg(start);

        while(fl_pos < end)
        {
            chunkSz = end - fl_pos < g_numColWidth ? end - fl_pos : g_numColWidth;
            fl_pos += chunkSz;

            // Read chunkSz number of bytes from file
            inFile.read(buff, chunkSz);
            if (inFile.fail())
                throw new Exception("Error reading file!");

            for(j = 0; j < chunkSz; j++)
            {
                uchr = (buff[j] < 0 || !isprint(buff[j])) ? 32 : buff[j];
                printf("%4.1c", (char)uchr);
            }

            printf("\n");
            for(j = 0; j < chunkSz; j++)
            {
                tmp = 0;
    //                wmemset(&tmp,0,sizeof(tmp));
                if(g_endianness == BYTES_LITTLE_ENDIAN)
                    memcpy(&tmp,&buff[j],1);
                else
                    memcpy((char*)(&tmp) + 3,buff,1);

                if (g_numberFormat == BYTES_HEX_FORMAT)
                    printf("%4.1X", tmp);
                else
                    printf("%4.1u", tmp);
            }
            printf("\n\n");

            if (++linesPrinted >= g_linesToPrint)
                break; // Already printed requested number of lines so exit
        }

        /****************************************************************************
        The following code block prints the output down the screen in one column.
        ****************************************************************************/

        //for(i = start; i < end; i++)
        //{
        //    ch = fgetc(fp);
        //    printf("char #: %d\tASCII:\t%d", i + 1, ch);
        //    if(ch != 10 && ch != 13)         don't print CR or LF.
        //    printf("\t%c", ch);
        //}
    }
    catch (Exception excp)
    {
        printf("%s", excp.msg);
        retVal = -1;
    }

    inFile.close();

    return retVal;
}

void HandleFileIOErr(char* filePath)
{
    char buff[1024];
    strerror_s(buff, 1024, errno);
    printf("  Error opening file: %s\n  errVal: %d, errStr: %s\n",
        filePath, errno, buff);

//#ifndef _WIN32
//    printf("  Error opening file: %s\n  errVal: %d, errStr: %s\n",
//        filePath, errno, strerror(errno));
//#endif

    PrintUsage();
}
