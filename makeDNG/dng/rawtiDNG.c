
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <math.h>
#include <tiffio.h>

#define ROT(x) (x)

#define TIFFTAG_AnalogBalance 50727

enum tiff_cfa_color {
    CFA_RED = 0,
    CFA_GREEN = 1,
    CFA_BLUE = 2,
};

enum cfa_pattern {
    CFA_BGGR = 0,
    CFA_GBRG,
    CFA_GRBG,
    CFA_RGGB,
    CFA_NUM_PATTERNS,
};

static const char cfa_patterns[4][CFA_NUM_PATTERNS] = {
    [CFA_BGGR] = {CFA_BLUE, CFA_GREEN, CFA_GREEN, CFA_RED},
    [CFA_GBRG] = {CFA_GREEN, CFA_BLUE, CFA_RED, CFA_GREEN},
    [CFA_GRBG] = {CFA_GREEN, CFA_RED, CFA_BLUE, CFA_GREEN},
    [CFA_RGGB] = {CFA_RED, CFA_GREEN, CFA_GREEN, CFA_BLUE},
};

#define IMAGE_TUNE_AWB_RGB_SIZE    1024
#define IMAGE_TUNE_AWB_YUV_SIZE    1024

/**
  \brief YUV/RAW data file header format
*/
typedef struct {

  uint32_t validDataStartOffset;///< offset in bytes from start of file where actual data is stored in file
  uint32_t magicNum;            ///< to indicate if this is a valid header
  uint32_t version;             ///< header format version number, 0x00010000, is v1.0
  uint32_t dataFormat;          ///< 0: raw data, 1: YUV422 data, 2: YUV420 data
  uint32_t dataWidth;           ///< in pixels
  uint32_t dataHeight;          ///< in lines
  uint32_t dataBytesPerLines;   ///< in bytes 
  uint32_t yuv420ChormaOffset;  ///< in bytes from start of valid data, only valid for YUV420 data, file offset is (validDataStartOffset+dataChormaOffset)
  uint32_t rawDataStartPhase;   ///< 0: R, 1: Gr, 2: Gb, 3: B  
  uint32_t rawDataBitsPerPixel; ///< 8..14 bits
  uint32_t rawDataFormat;       ///< 0: Normal 1pixel in 16-bits, no compression, 1: Alaw compressed, 2: Dpcm compressed
  uint32_t H3aRegs[32];            ///< H3A Register Dump  - AWB
  uint32_t AwbNumWinH;          ///< Number of AWB windows in H direction
  uint32_t AwbNumWinV;          ///< Number of AWB windows in V direction
  uint32_t AwbRgbDataOffset;  ///< Offset in bytes from start of file where RGB H3A data is present
  uint32_t AwbYuvDataOffset;  ///< Offset in bytes from start of file where YUV H3A data is present
  uint32_t AwbMiscData[16];     ///< AWB algorithm specific data
  uint32_t AwbRgbData[3*IMAGE_TUNE_AWB_RGB_SIZE];    ///< Valid data is 4x3xN bytes where N = AwbNumWinH x AwbNumWinV
  uint32_t AwbYuvData[3*IMAGE_TUNE_AWB_YUV_SIZE];    ///< Valid data is 4x3xN bytes where N = AwbNumWinH x AwbNumWinV

} IMAGE_TUNE_SaveDataFileHeader;


int main (int argc, char **argv)
{
	static const short CFARepeatPatternDim[] = { 2,2 };
	static const float cam_xyz[] =
   // { 2.005,-0.771,-0.269, -0.752,1.688,0.064, -0.149,0.283,0.745 }; // default
    // {3.2404542, -1.5371385, -0.4985314, -0.9692660,  1.8760108,  0.0415560,0.0556434, -0.2040259,  1.0572252 }; // sRGB
    { 2.0413690, -0.5649464, -0.3446944, -0.9692660,  1.8760108,  0.0415560,0.0134474, -0.1183897,  1.0154096 }; // Adobe RGB
	static const float neutral[] = { 1.0, 1.0, 1.0};
    static const float balance[] = { 1.5, 1.0, 1.5};
    long sub_offset=0, white=0xfff, black=0;
	float gam;
	int status=1, i, r, c, row, col;
	unsigned short curve[256];
	unsigned char *out;
	struct stat st;
	struct tm tm;
	char datetime[64];
	FILE *ifp;
	TIFF *tif;
	IMAGE_TUNE_SaveDataFileHeader rawTI;
	int width, height;
	
	for (i=0; i < 256; i++)
		curve[i] = 0xfff * pow (i/255.0,1) + 0.5;
	
	if (!(ifp = fopen (argv[1], "rb"))) {
		perror (argv[1]);
		return 1;
	}
	stat (argv[1], &st);
	gmtime_r (&st.st_mtime, &tm);
	sprintf (datetime, "%04d:%02d:%02d %02d:%02d:%02d",
			 tm.tm_year+1900,tm.tm_mon+1,tm.tm_mday,tm.tm_hour,tm.tm_min,tm.tm_sec);

	// Read the rawTI header
	if (fread (&rawTI, sizeof(rawTI), 1, ifp) != 1) {
		perror ("Failed to read rawTI header\n");
		return 1;
	}

	// Pull out the width/height
	width = rawTI.dataWidth;
	height = rawTI.dataHeight;
	
	// Skip padding after heading to the raw data
	fseek(ifp, rawTI.validDataStartOffset, SEEK_SET);

	if (!(tif = TIFFOpen (argv[4], "w"))) goto fail;
	
	TIFFSetField (tif, TIFFTAG_SUBFILETYPE, 1);
	TIFFSetField (tif, TIFFTAG_IMAGEWIDTH, width>>4);
	TIFFSetField (tif, TIFFTAG_IMAGELENGTH, height>>4);
	TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, 8);
	TIFFSetField (tif, TIFFTAG_COMPRESSION, COMPRESSION_NONE);
	TIFFSetField (tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	TIFFSetField (tif, TIFFTAG_MAKE, "Prosilica/AVT");
	TIFFSetField (tif, TIFFTAG_MODEL, "GX1910C");
	TIFFSetField (tif, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
	TIFFSetField (tif, TIFFTAG_SAMPLESPERPIXEL, 3);
	TIFFSetField (tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField (tif, TIFFTAG_SOFTWARE, "field_dng");
	TIFFSetField (tif, TIFFTAG_DATETIME, datetime);
	TIFFSetField (tif, TIFFTAG_SUBIFD, 1, &sub_offset);
	TIFFSetField (tif, TIFFTAG_DNGVERSION, "\001\001\0\0");
	TIFFSetField (tif, TIFFTAG_DNGBACKWARDVERSION, "\001\0\0\0");
	TIFFSetField (tif, TIFFTAG_UNIQUECAMERAMODEL, "GX1910C");
	TIFFSetField (tif, TIFFTAG_COLORMATRIX1, 9, cam_xyz);
	TIFFSetField (tif, TIFFTAG_ASSHOTNEUTRAL, 3, neutral);
//  TIFFSetField (tif, TIFFTAG_ASSHOTWHITEXY, 2, whitebal);
    TIFFSetField (tif, TIFFTAG_CALIBRATIONILLUMINANT1, 21); //D65
    TIFFSetField (tif, TIFFTAG_AnalogBalance, 3,balance);
//  TIFFSetField (tif, TIFFTAG_ORIGINALRAWFILENAME, argv[2]);
	
	char *blackBuf = (char *)malloc(width>>4);
	
	
	memset (blackBuf, 0, width>>4);// all-black thumbnail
	for (row=0; row < height>> 4; row++)
//		TIFFWriteScanline (tif, blackBuf, row, 0);
//	TIFFWriteDirectory (tif);
	
	TIFFSetField (tif, TIFFTAG_SUBFILETYPE, 0);
	TIFFSetField (tif, TIFFTAG_IMAGEWIDTH, width);
	TIFFSetField (tif, TIFFTAG_IMAGELENGTH, height);
	TIFFSetField (tif, TIFFTAG_BITSPERSAMPLE, 16);
	TIFFSetField (tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CFA);
	TIFFSetField (tif, TIFFTAG_SAMPLESPERPIXEL, 1);
	TIFFSetField (tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
	TIFFSetField (tif, TIFFTAG_CFAREPEATPATTERNDIM, CFARepeatPatternDim);
	TIFFSetField (tif, TIFFTAG_CFAPATTERN, 4, cfa_patterns[2]);
//   TIFFSetField (tif, 50738, 0.0); // AntiAliasStrength
	TIFFSetField (tif, TIFFTAG_WHITELEVEL, 1, &white);
	TIFFSetField (tif, TIFFTAG_BLACKLEVEL, 1, &black);
	
	out = calloc (width*2, 1);
	
	for (row=0; row < height; row ++) {
		for(col=0;col<width*2;col++)
		{
			int m = 0;
			out[col+m] = fgetc(ifp);
		}
		
		TIFFWriteScanline (tif, out, row, 0);
	}
	
	free (out);
	TIFFClose (tif);
	status = 0;
fail:
	fclose (ifp);
	return status;
}
