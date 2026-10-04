/* TJpgDec configuration for Vellum: grayscale output, descaling on. */
#define JD_SZBUF 1024        /* stream input buffer */
#define JD_FORMAT 2          /* 2: grayscale, 8 bits per pixel */
#define JD_USE_SCALE 1       /* decode at 1/2, 1/4 or 1/8 size */
#define JD_TBLCLIP 1         /* table saturation */
#define JD_FASTDECODE 1      /* 32-bit barrel shifter */
#define JD_DEFAULT_HUFFMAN 0 /* covers carry their own tables */
