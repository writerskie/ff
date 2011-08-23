/* fontengines/freetype/FontEngineFT.cpp
**
** Copyright 2006, The Android Open Source Project
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
** This file uses much of the code from SkFontHost_FreeType.cpp
*/

#include <assert.h>
#include <utils/threads.h>
#include <utils/FontEngineManager.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_SIZES_H
#include FT_TRUETYPE_TABLES_H
#include FT_TYPE1_TABLES_H
#include FT_BITMAP_H
// In the past, FT_GlyphSlot_Own_Bitmap was defined in this header file.
#include FT_SYNTHESIS_H
#include FT_XFREE86_H

#if defined(SUPPORT_LCDTEXT)
#include FT_LCD_FILTER_H
#endif

#ifdef   FT_ADVANCES_H
#include FT_ADVANCES_H
#endif

//#define ENABLE_GLYPH_SPEW     // for tracing calls

/* If the following macro is enabled; then list of font instance will be
   maintained by every font object. While creating an font instance this list
   will be searched. If similar font instance found while searching than that
   instance will be retured; a new instance will be created and returned
   otherwise.
*/
#define ENABLE_FONTINSTLIST

//#define FT_ENABLE_LOG

#ifdef FT_ENABLE_LOG
/* fprintf, FILE */
#include <stdio.h>

static FILE * fplog = NULL;

#define FT_STARTLOG fplog = fopen("/data/ftlog.txt", "a");
#define FT_LOG(...) \
        FT_STARTLOG \
        fprintf(fplog, __FUNCTION__); \
        fprintf(fplog, ", "); \
        fprintf(fplog, __VA_ARGS__); \
        FT_ENDLOG
#define FT_ENDLOG fclose(fplog);
#else
#define FT_STARTLOG
#define FT_LOG(...)
#define FT_ENDLOG
#endif /* FT_ENABLE_LOG */

#ifdef FT_ENABLE_LOG
    #define FT_ASSERT_CONTINUE(pred)                                                      \
        do {                                                                              \
            if (!(pred))                                                                  \
                FT_LOG("file %s:%d: assert failed '" #pred "'\n", __FILE__, __LINE__);    \
        } while (false)
#else
    #define FT_ASSERT_CONTINUE(pred)
#endif

////////////////////////////////////////////////////////////////////////

/* Following are adopted from Skia */

#define FEM16Dot16ToFEM26Dot6(x)   ((x) >> 10)
#define FEM26Dot6ToFEM16Dot16(x)   ((x) << 10)

#define FEM16Dot16Avg(a, b)    (((a) + (b)) >> 1)

/** Returns -1 if n < 0, else returns 0
*/
#define FEM16Dot16ExtractSign(n)    ((int32_t)(n) >> 31)

#define FEM16Dot16Invert(n)         DivBits(FEMOne16Dot16, n, 16)

#define MaxS32   0x7FFFFFFF

#define R16_BITS    5
#define G16_BITS    6
#define B16_BITS    5

#define R16_MASK    ((1 << R16_BITS) - 1)
#define G16_MASK    ((1 << G16_BITS) - 1)
#define B16_MASK    ((1 << B16_BITS) - 1)

#define R16_SHIFT    (B16_BITS + G16_BITS)
#define G16_SHIFT    (B16_BITS)
#define B16_SHIFT    0

// FREETYPE_LCD_LERP should be 0...256
// 0 means no color reduction (e.g. just as returned from FreeType)
// 256 means 100% color reduction (e.g. gray)
//
#ifndef FREETYPE_LCD_LERP
    #define FREETYPE_LCD_LERP    96
#endif

static inline int32_t FEM16Dot16Abs(int32_t value);

/** If sign == -1, returns -n, else sign must be 0, and returns n.
    Typically used in conjunction with FEM16Dot16ExtractSign().
*/
static inline int32_t FEM16Dot16ApplySign(int32_t n, int32_t sign);

/** Computes (numer1 << shift) / denom in full 64 intermediate precision.
    It is an error for denom to be 0. There is no special handling if
    the result overflows 32bits.
*/
static int32_t DivBits(int32_t numer, int32_t denom, int shift);

static inline uint16_t packRGB16(unsigned r, unsigned g, unsigned b);

static int lerp(int start, int end);

static uint16_t packTriple(unsigned r, unsigned g, unsigned b);

static void copyFT2LCD16(uint32_t rowBytes, uint16_t width, uint16_t height, uint16_t *buffer, const FT_Bitmap& bitmap);

inline int32_t FEM16Dot16Abs(int32_t value)
{
    int32_t  mask = value >> 31;
    return (value ^ mask) - mask;
}/* end method FEM16Dot16Abs */

inline int32_t FEM16Dot16ApplySign(int32_t n, int32_t sign)
{
    assert(sign == 0 || sign == -1);
    return (n ^ sign) - sign;
}/* end method FEM16Dot16ApplySign */


#define DIVBITS_ITER(n)                                 \
    case n:                                             \
        if ((numer = (numer << 1) - denom) >= 0)        \
            result |= 1 << (n - 1); else numer += denom

int32_t DivBits(int32_t numer, int32_t denom, int shift_bias) {
    assert(denom != 0);
    if (numer == 0) {
        return 0;
    }/* end if */

    // make numer and denom positive, and sign hold the resulting sign
    int32_t sign = FEM16Dot16ExtractSign(numer ^ denom);
    numer = FEM16Dot16Abs(numer);
    denom = FEM16Dot16Abs(denom);

    int nbits = __builtin_clz(numer) - 1;
    int dbits = __builtin_clz(denom) - 1;
    int bits = shift_bias - nbits + dbits;

    if (bits < 0) {  // answer will underflow
        return 0;
    }/* end if */
    if (bits > 31) {  // answer will overflow
        return FEM16Dot16ApplySign(MaxS32, sign);
    }/* end if */

    denom <<= dbits;
    numer <<= nbits;

    FEM16Dot16 result = 0;

    // do the first one
    if ((numer -= denom) >= 0) {
        result = 1;
    } else {
        numer += denom;
    }/* end else if */

    // Now fall into our switch statement if there are more bits to compute
    if (bits > 0) {
        // make room for the rest of the answer bits
        result <<= bits;
        switch (bits) {
            DIVBITS_ITER(31); DIVBITS_ITER(30); DIVBITS_ITER(29);
            DIVBITS_ITER(28); DIVBITS_ITER(27); DIVBITS_ITER(26);
            DIVBITS_ITER(25); DIVBITS_ITER(24); DIVBITS_ITER(23);
            DIVBITS_ITER(22); DIVBITS_ITER(21); DIVBITS_ITER(20);
            DIVBITS_ITER(19); DIVBITS_ITER(18); DIVBITS_ITER(17);
            DIVBITS_ITER(16); DIVBITS_ITER(15); DIVBITS_ITER(14);
            DIVBITS_ITER(13); DIVBITS_ITER(12); DIVBITS_ITER(11);
            DIVBITS_ITER(10); DIVBITS_ITER( 9); DIVBITS_ITER( 8);
            DIVBITS_ITER( 7); DIVBITS_ITER( 6); DIVBITS_ITER( 5);
            DIVBITS_ITER( 4); DIVBITS_ITER( 3); DIVBITS_ITER( 2);
            // we merge these last two together, makes GCC make better ARM
            default:
            DIVBITS_ITER( 1);
        }/* end switch  */
    }/* end if */

    if (result < 0) {
        result = MaxS32;
    }/* end if */
    return FEM16Dot16ApplySign(result, sign);
}/* end method DivBits */

inline uint16_t packRGB16(unsigned r, unsigned g, unsigned b) {
    assert(r <= R16_MASK);
    assert(g <= G16_MASK);
    assert(b <= B16_MASK);

    return (uint16_t)((r << R16_SHIFT) | (g << G16_SHIFT) | (b << B16_SHIFT));
}/* end method packRGB16 */

int lerp(int start, int end) {
    assert((unsigned)FREETYPE_LCD_LERP <= 256);
    return start + ((end - start) * (FREETYPE_LCD_LERP) >> 8);
}/* end method lerp */

uint16_t packTriple(unsigned r, unsigned g, unsigned b) {
    if (FREETYPE_LCD_LERP) {
        // want (a+b+c)/3, but we approx to avoid the divide
        unsigned ave = (5 * (r + g + b) + b) >> 4;
        r = lerp(r, ave);
        g = lerp(g, ave);
        b = lerp(b, ave);
    }/* end if */
    return packRGB16(r >> 3, g >> 2, b >> 3);
}/* end method packTriple */

void copyFT2LCD16(uint32_t rowBytes, uint16_t width, uint16_t height, uint16_t *buffer, const FT_Bitmap& bitmap) {
    assert(width * 3 == bitmap.width - 6);
    assert(height == bitmap.rows);

    const uint8_t* src = bitmap.buffer + 3;
    uint16_t* dst = buffer;
    size_t dstRB = rowBytes;

    for (int y = 0; y < height; y++) {
        const uint8_t* triple = src;
        for (int x = 0; x < width; x++) {
            dst[x] = packTriple(triple[0], triple[1], triple[2]);
            triple += 3;
        }/* end for */
        src += bitmap.pitch;
        dst = (uint16_t*)((char*)dst + dstRB);
    }/* end for */
}/* end method copyFT2LCD16 */
////////////////////////////////////////////////////////////////////////

#ifdef FT_ENABLE_LOG
#define FTEnginePrintList FT_PrintList((BasicNodePtr) gFontEngineInstFT->getList())
#else /* ! FT_ENABLE_LOG */
#define FTEnginePrintList
#endif /* ! FT_ENABLE_LOG */

#define FontFTAddAtHead(__l, __r) FT_AddAtHead((BasicNodePtr*)__l, (BasicNodePtr)__r)
//////////////////////////////////////////////////////////////////////////

class FontEngineFT;
class FontFT;
class FontInstFT;
class FontScalerFT;

static android::Mutex  gMutexFT;
static int             gCountFontFT;
static FT_Library      gLibraryFT;
static bool            gLCDSupportValid;  /* true iff |gLCDSupport| has been set. */
static bool            gLCDSupport;  /* true iff LCD is supported by the runtime. */

static FontEngineFT*   gFontEngineInstFT = NULL;  /* global plugin engine instance */

// See http://freetype.sourceforge.net/freetype2/docs/reference/ft2-bitmap_handling.html#FT_Bitmap_Embolden
// This value was chosen by eyeballing the result in Firefox and trying to match it.
static const FT_Pos kBitmapEmboldenStrength = 1 << 6;

typedef struct BasicNode_t  BasicNode;
typedef BasicNode*          BasicNodePtr;

struct BasicNode_t
{
    BasicNodePtr  next;
};

typedef struct FontNode_t  FontNode;
typedef FontNode*          FontNodePtr;

/* font list */
struct FontNode_t
{
    FontNodePtr  next;
    FontFT*      font;
};/* end struct FontNode_t */

class FontEngineFT : public FontEngine
{
public:
    FontEngineFT()
        : name("freetype"), pFontList(NULL)
    {
        FT_LOG("%s engine instance created\n", name);
    }

    ~FontEngineFT() {}

    /* Return Font engine name */
    const char* getName() const { return name; }

    /* Return font engine capabilities */
    fem::EngineCapability getCapabilities(FontScalerInfo& desc) const;

    /*
     * Given system path to font file; return font's name and style. It also
     * return a flag which tells about whether the font fixed width.
     */
    size_t getFontNameAndAttribute(const char path[], char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth);

    /*
     * Given buffer to font file and buffer length; return font's name and
     * style. It also return a flag which tells about whether the font fixed
     * width.
     */
    size_t getFontNameAndAttribute(const void* buffer, const uint32_t bufferLength, char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth);

    /* Given system path to font file; return 'true' if the font format
     * supported; 'false' otherwise.
     */
    bool isFontSupported(const char path[], bool isLoad);

    /* Given a buffer to font file; return 'true' if the font format supported;
     * 'false' otherwise.
     */
    bool isFontSupported(const void* buffer, const uint32_t bufferLength);

    /* Create and return font scaler */
    FontScaler* createFontScalerContext(const FontScalerInfo& desc);

    /** Given system path of the font file; returns the number of font units
       per em.
       @param path    The system path to font file.
       @return the number of font units per em or 0 on error.
    */
    uint32_t getFontUnitsPerEm(const char path[]);

    /** Given font data in buffer; returns the number of font units per em.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @return the number of font units per em or 0 on error.
    */
    uint32_t getFontUnitsPerEm(const void* buffer, const uint32_t bufferLength);

    /** Returned whether the font is embeddable or not.
        @param path    The system path to font file.
        @return true in case font is embeddable; false otherwise.
    */
    bool canEmbed(const char path[]);

    /** The API tells whether the font is embeddable or not.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @return true in case font is embeddable; false otherwise.
    */
    bool canEmbed(const void* buffer, const uint32_t bufferLength);

    /** Given system path of the font file; returns the unhinted advances
        in font units.
        @param path              The system path to font file.
        @param start             The first glyph index..
        @param count             The number of glyph name values you want to
                                 retrieve.
        @param pGlyphsAdvance    The advances, in font units. This array must
                                 contain at least 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    uint32_t getGlyphsAdvance(const char path[], uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance);

    /** Given font data in buffer; returns the number of font units per em.
        @param buffer            The font file buffer.
        @param bufferLength      Length of the buffer.
        @param start             The first glyph index..
        @param count             The number of glyph name values you want to
                                 retrieve.
        @param pGlyphsAdvance    The advances, in font units. This array must
                                 contain at least 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    uint32_t getGlyphsAdvance(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance);

    /** Given system path of the font file; returns the glyph names.
        @param path           The system path to font file.
        @param start          The first glyph index..
        @param count          The number of glyph name values you want to
                              retrieve.
        @param pGlyphsName    The glyph names. This array must contain at
                              least 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    uint32_t getGlyphsName(const char path[], uint32_t start, uint32_t count, char** pGlyphsName);

    /** Given font data in buffer; returns the glyph names.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @param start           The first glyph index..
        @param count           The number of glyph name values you want to
                               retrieve.
        @param pGlyphsName     The glyph names. This array must contain at
                               least 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    uint32_t getGlyphsName(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, char** pGlyphsName);


    /** Given system path of the font file; returns the glyph unicodes.
        @param path              The system path to font file.
        @param start             The first glyph index..
        @param count             The number of unicode values you want to retrieve.
        @param pGlyphsUnicode    The glyph unicodes. This array must contain at least
                                 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    uint32_t getGlyphsUnicode(const char path[], uint32_t start, uint32_t count, int32_t* pGlyphsUnicode);

    /** Given font data in buffer; returns the glyph unicodes.
        @param buffer            The font file buffer.
        @param bufferLength      Length of the buffer.
        @param start             The first glyph index..
        @param count             The number of unicode values you want to
                                 retrieve.
        @param pGlyphsUnicode    The glyph unicodes. This array must contain at least
                                 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    uint32_t getGlyphsUnicode(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, int32_t* pGlyphsUnicode);

    /** Retrieve detailed typeface metrics. Used by the PDF backend.
        @param path    The system path to font file.
        @return A pointer to vaild object on success; NULL is returned if
        the font is not found.
    */
    AdvancedTypefaceMetrics* getAdvancedTypefaceMetrics(const char path[]);

    /** Retrieve detailed typeface metrics. Used by the PDF backend.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @return A pointer to vaild object on success; NULL is returned if
        the font is not found.
    */
    AdvancedTypefaceMetrics* getAdvancedTypefaceMetrics(const void* buffer, const uint32_t bufferLength);

private:
    FontScaler* getFontScaler(const FontScalerInfo& desc);
    FontFT* getFont(const FontScalerInfo& desc);
    FontNodePtr getList() { return this->pFontList; }

    const char* name;
    FontNodePtr  pFontList;

    /* Array of supported font formats. */
    static const char* const formats[];

    friend class FontFT;
    friend class FontScalerFT;
};/* end class FontEngineFT */

const char* const FontEngineFT::formats[] = { "ttf", NULL };

#ifdef ENABLE_FONTINSTLIST
typedef struct FontInstNode_t  FontInstNode;
typedef FontInstNode*          FontInstNodePtr;

/* font instances list */
struct FontInstNode_t
{
    FontInstNodePtr  next;
    FontInstFT*      inst;
};/* end struct FontInstNode_t */
#endif /* ENABLE_FONTINSTLIST */

class FontFT
{
public:
    FontFT(const FontScalerInfo& desc);
    ~FontFT();

    FontScaler* getFontScaler(const FontScalerInfo& desc);
    bool success() { return bInitialized; }

private:
    FontFT(FontFT&);
    FontFT& operator = (FontFT&);

#ifdef ENABLE_FONTINSTLIST
    FontInstNodePtr searchFontInst(const FontScalerInfo& desc);
    FontInstNodePtr getList() { return this->pFontInstList; }
#endif /* ENABLE_FONTINSTLIST */

    void getTransMatrix(const FontScalerInfo& desc, FT_Matrix& ftMatrix22, FEM16Dot16& fScaleX, FEM16Dot16& fScaleY, uint32_t& loadGlyphFlags);

    FT_StreamRec      streamRecFT;
    FT_Face           pFace;   /* we own this */

    uint32_t          fontID;
    const char*       pPath;   /* we own this */
    const uint8_t*    pBuffer; /* font file buffer */

    bool              bInitialized;
    uint16_t          refCnt;

#ifdef ENABLE_FONTINSTLIST
    FontInstNodePtr   pFontInstList;
#endif /* ENABLE_FONTINSTLIST */

    friend class FontScalerFT;
    friend class FontEngineFT;
    friend class FontInstFT;
};

class FontInstFT
{
public :
    FontInstFT(const FontScalerInfo& desc, FontFT* font);
    ~FontInstFT();

    FT_Error setupSize();

    bool success() { return bInitialized; }

private:
    /* Specify the kerning, hinting, emboldening and embedded-bitmap status
       for the font scaler.
    */
    uint8_t          fontInstFlags;

    bool             subpixelPositioning;
    FEM16Dot16       fScaleX, fScaleY;
    FT_Matrix        ftMatrix22;
    FT_Size          ftSize;
    uint32_t         loadGlyphFlags;
    fem::AliasMode   maskFormat;  /* mono, gray, lcd */

    FontFT          *pFont;
    uint16_t         refCnt;

    bool             bInitialized;

    friend class FontFT;
    friend class FontScalerFT;
};/* end class FontInstFT */

class FontScalerFT : public FontScaler
{
public:
    FontScalerFT(FontInstFT* fontInst);
    ~FontScalerFT();

    bool success() { return bInitialized; }

    uint16_t getGlyphCount() const;
    uint16_t getCharToGlyphID(int32_t charUniCode);
    int32_t getGlyphIDToChar(uint16_t glyphID);
    GlyphMetrics getGlyphAdvance(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY);
    GlyphMetrics getGlyphMetrics(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY);
    void getGlyphImage(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY, uint32_t rowBytes, uint16_t width, uint16_t height, uint8_t *buffer);
    void getFontMetrics(FontMetrics* mX, FontMetrics* mY);
    GlyphOutline* getGlyphOutline(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY);

private:
    void emboldenOutline(FT_Outline* outline);

    FontInstFT*  pFontInst;
    FT_Face      ftFace;        /* convinence pointer */

    bool         bInitialized;

    friend class FontFT;
    friend class FontInstFT;
};/* end class FontScalerFT */

/**
 * Global Methods.
 */
#ifdef FT_ENABLE_LOG
static void FT_PrintList(BasicNodePtr head)
{
    while (head != NULL) {
        FT_LOG("%x->", head);
        head = head->next;
    }/* end while */

    FT_LOG("NULL\n");
}/* end method FT_PrintList */
#endif /* FT_ENABLE_LOG */

static void FT_AddAtHead(BasicNodePtr *list,  BasicNodePtr r)
{
    r->next = *list;
    *list = r;
}/* end method FT_AddAtHead */

static bool InitFreetype()
{
    FT_Error err = FT_Init_FreeType(&gLibraryFT);
    if (err) {
        FT_LOG("failed to initalized FreeType\n");
        return false;
    }/* end if */

#if defined(SUPPORT_LCDTEXT)
    /* Setup LCD filtering. This reduces colour fringes for LCD rendered glyphs. */
    err = FT_Library_SetLcdFilter(gLibraryFT, FT_LCD_FILTER_DEFAULT);
    gLCDSupport = err == 0;
#endif
    gLCDSupportValid = true;

    return true;
}/* end method InitFreetype */

/**
 * FontEngineFT
 */
fem::EngineCapability FontEngineFT::getCapabilities(FontScalerInfo& desc) const
{
    FT_UNUSED(desc);
    return fem::EngineCapability(fem::CAN_RENDER_MONO | fem::CAN_RENDER_GRAY);
}/* end method getCapabilities */

/*
 * Given system path to font file; return font's name and style. It also
 * return a flag which tells about whether the font fixed width.
 */
size_t FontEngineFT::getFontNameAndAttribute(const char path[], char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth)
{
    FT_Library  library;
    size_t count = 0;

    assert(path);

    if (path) {
        FT_Face face;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
            return 0;
        }/* end if */

        if (FT_New_Face(library, path, 0, &face)) {
            FT_LOG("failed to create FT_Face\n");
            FT_Done_FreeType(library);
            return 0;
        }/* end if */

        const char* s = face->family_name;
        FT_LOG("family_name : %s\n", face->family_name);

        while (s[count] != '\0') {
            count++;
        }/* end while */

        if (name && length && style && isFixedWidth) {
            int fntStyle = fem::STYLE_NORMAL;

            memset(name, 0, length);

            if (length < count) {
                FT_LOG("copying %lu bytes to name\n", (unsigned long)length);

                count = 0;
                while(length) {
                    name[count] = s[count];
                    count++; length--;
                }/* end while */
            } else {
                FT_LOG("copying %lu bytes to name\n", (unsigned long)count);

                length = 0;  /* avoiding buffer over flow */
                while(count) {
                    name[length] = s[length];
                    length++; count--;
                }/* end while */

                count = length;
            }/* end else if */

            FT_LOG("name : %s\n", name);
            if (face->style_flags & FT_STYLE_FLAG_BOLD) {
                    fntStyle |= fem::STYLE_BOLD;
            }/* end if */

            if (face->style_flags & FT_STYLE_FLAG_ITALIC) {
                fntStyle |= fem::STYLE_ITALIC;
            }/* end if */

            *style = (fem::FontStyle)fntStyle;
            FT_LOG("style : %d\n", *style);

            *isFixedWidth = FT_IS_FIXED_WIDTH(face);
        }/* end if */

        FT_Done_Face(face);
        FT_Done_FreeType(library);
    }/* end if */

    FT_LOG("length : %lu\n", (unsigned long)count);
    return count;
}/* end method getFontNameAndAttribute */

/*
 * Given buffer to font file and buffer length; return font's name and
 * style. It also return a flag which tells about whether the font fixed
 * width.
 */
size_t FontEngineFT::getFontNameAndAttribute(const void* buffer, const uint32_t bufferLength, char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth)
{
    FT_Library  library;
    size_t count = 0;

    assert(buffer && bufferLength);

    if (buffer && bufferLength) {
        FT_Open_Args  args;
        FT_Face  face;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
            return 0;
        }/* end if */

        memset(&args, 0, sizeof(args));

        args.flags = FT_OPEN_MEMORY;
        args.memory_base = (const FT_Byte*)buffer;
        args.memory_size = bufferLength;

        if (FT_Open_Face(library, &args, 0, &face)) {
            FT_LOG("failed to create FT_Face\n");
            FT_Done_FreeType(library);
            return 0;
        }/* end if */

        const char* s = face->family_name;
        FT_LOG("family_name : %s\n", face->family_name);

        while (s[count] != '\0') {
            count++;
        }/* end while */

        if (name && length && style && isFixedWidth) {
            int fntStyle = fem::STYLE_NORMAL;

            memset(name, 0, length);

            if (length < count) {
                FT_LOG("copying %lu bytes to name\n", (unsigned long)length);

                count = 0;
                while(length) {
                    name[count] = s[count];
                    count++; length--;
                }/* end while */
            } else {
                FT_LOG("copying %lu bytes to name\n", (unsigned long)count);

                length = 0;  /* avoiding buffer over flow */
                while(count) {
                    name[length] = s[length];
                    length++; count--;
                }/* end while */

                count = length;
            }/* end else if */

            FT_LOG("name : %s\n", name);
            if (face->style_flags & FT_STYLE_FLAG_BOLD) {
                fntStyle |= fem::STYLE_BOLD;
            }/* end if */

            if (face->style_flags & FT_STYLE_FLAG_ITALIC) {
                fntStyle |= fem::STYLE_ITALIC;
            }/* end if */

            *style = (fem::FontStyle)fntStyle;
            FT_LOG("style : %d\n", *style);

            *isFixedWidth = FT_IS_FIXED_WIDTH(face);
        }/* end if */

        FT_Done_Face(face);
        FT_Done_FreeType(library);
    }/* end if */

    FT_LOG("length : %lu\n", (unsigned long)count);
    return count;
}/* end method getFontNameAndAttribute */

/* Given system path to font file; return 'true' if the font format
 * supported; 'false' otherwise.
 *
 * If 'isLoad' is set to 'true' then; font file will be loaded i.e. font
 * objects (sfnt etc.) will be created to determine the font file support.
 * If 'isLoad' is set to 'false' then; the file extension will be checked
 * against the static font format list of the Font Engine.
 */
bool FontEngineFT::isFontSupported(const char path[], bool isLoad)
{
    FT_Library  library;
    bool        retVal = false;

    assert(path);

    if (path) {
        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
            goto RETURN;
        }/* end if */

        if (isLoad) {
            FT_Face   face;
            FT_Error  error;

            error = FT_New_Face(library, path, 0, &face);
            if (error == FT_Err_Unknown_File_Format) {
                FT_Done_FreeType(library);
                FT_LOG("unsupported font format\n");
                goto RETURN;
            } else if (error) {
                FT_Done_FreeType(library);
                FT_LOG("failed to create FT_Face\n");
                goto RETURN;
            }/* end else if */

            retVal = true;

            FT_Done_Face(face);
            FT_Done_FreeType(library);
        } else {
            unsigned int len = strlen(path);
            unsigned int idx = len - 3;
            const char* fileExtension = &path[idx];

            if (idx > 0) {
                size_t  formatCount = sizeof(formats) / sizeof(formats[0]);

                for (size_t i = 0; i < formatCount; i++) {
                    if (! strcmp(formats[i], fileExtension)) {
                        retVal = true;
                        break;
                    }/* end if */
                }/* end for */
            }/* end if */
        }/* end else if */
    }/* end if */

RETURN:
    return retVal;
}/* end method isFontSupported */

/* Given a buffer to font file; return 'true' if the font format supported;
 * 'false' otherwise.
 *
 * Font file will be loaded i.e. font object (sfnt etc.) will be created to
 * determine the font file support.
 */
bool FontEngineFT::isFontSupported(const void* buffer, const uint32_t bufferLength)
{
    FT_Library    library;
    bool          retVal = false;

    assert(buffer && bufferLength);

    if (buffer && bufferLength) {
        FT_Open_Args  args;
        FT_Face       face;
        FT_Error      error;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
            goto RETURN;
        }/* end if */

        memset(&args, 0, sizeof(args));

        args.flags = FT_OPEN_MEMORY;
        args.memory_base = (const FT_Byte*)buffer;
        args.memory_size = bufferLength;

        error = FT_Open_Face(library, &args, 0, &face);
        if (error == FT_Err_Unknown_File_Format) {
            FT_Done_FreeType(library);
            FT_LOG("unsupported font format\n");
            goto RETURN;
        } else if (error) {
            FT_Done_FreeType(library);
            FT_LOG("failed to create FT_Face\n");
            goto RETURN;
        }/* end else if */

        retVal = true;

        FT_Done_Face(face);
        FT_Done_FreeType(library);
    }/* end if */

RETURN:
    return retVal;
}/* end method isFontSupported */

/** Given system path of the font file; returns the number of font units
    per em.
    @param path    The system path to font file.
    @return the number of font units per em or 0 on error.
*/
uint32_t FontEngineFT::getFontUnitsPerEm(const char path[])
{
    FT_Library  library;
    uint32_t    unitsPerEm = 0;

    assert(path);

    if (path) {
        FT_Face  face;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
            goto RETURN;
        }/* end if */

        if (FT_New_Face(library, path, 0, &face)) {
            FT_LOG("failed to create FT_Face\n");
            FT_Done_FreeType(library);
            goto RETURN;
        }/* end if */

        unitsPerEm = face->units_per_EM;
        FT_LOG("units per em : %u\n", unitsPerEm);

        FT_Done_Face(face);
        FT_Done_FreeType(library);
    }/* end if */

RETURN:
    return unitsPerEm;
}/* end method getFontUnitsPerEm */

/** Given font data in buffer; returns the number of font units per em.
    @param buffer          The font file buffer.
    @param bufferLength    Length of the buffer.
    @return the number of font units per em or 0 on error.
*/
uint32_t FontEngineFT::getFontUnitsPerEm(const void* buffer, const uint32_t bufferLength)
{
    FT_Library  library;
    uint32_t    unitsPerEm = 0;

    assert(buffer && bufferLength);

    if (buffer && bufferLength) {
        FT_Open_Args  args;
        FT_Face       face;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
            goto RETURN;
        }/* end if */

        memset(&args, 0, sizeof(args));

        args.flags = FT_OPEN_MEMORY;
        args.memory_base = (const FT_Byte*)buffer;
        args.memory_size = bufferLength;

        if (FT_Open_Face(library, &args, 0, &face)) {
            FT_LOG("failed to create FT_Face\n");
            FT_Done_FreeType(library);
            goto RETURN;
        }/* end if */

        unitsPerEm = face->units_per_EM;
        FT_LOG("units per em : %u\n", unitsPerEm);

        FT_Done_Face(face);
        FT_Done_FreeType(library);
    }/* end if */

RETURN:
    return unitsPerEm;
}/* end method getFontUnitsPerEm */

/** Returned whether the font is embeddable or not.
    @param path    The system path to font file.
    @return true in case font is embeddable; false otherwise.
*/
bool FontEngineFT::canEmbed(const char path[])
{
#if defined(SK_BUILD_FOR_MAC) || defined(ANDROID)
    return false;
#else
    FT_Library  library;
    bool        retVal = false;

    assert(path);

    if (path) {
        FT_Face  face;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            if (FT_New_Face(library, path, 0, &face)) {
                FT_LOG("failed to create FT_Face\n");
            } else {
#ifdef FT_FSTYPE_RESTRICTED_LICENSE_EMBEDDING
                FT_UShort  fsType = FT_Get_FSType_Flags(face);
                retVal = ((fsType & (FT_FSTYPE_RESTRICTED_LICENSE_EMBEDDING | FT_FSTYPE_BITMAP_EMBEDDING_ONLY)) == 0) ? true : false;
#else
                // No embedding is 0x2 and bitmap embedding only is 0x200.
                TT_OS2* os2_table;
                if ((os2_table = (TT_OS2*)FT_Get_Sfnt_Table(face, ft_sfnt_os2)) != NULL) {
                    retVal = ((os2_table->fsType & 0x202) == 0) ? true : false;
                }/* end if */
#endif
                FT_LOG("canEmbed: %s\n", retVal ? "yes" : "no");

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
#endif
}/* end method canEmbed */


/** The API tells whether the font is embeddable or not.
    @param buffer          The font file buffer.
    @param bufferLength    Length of the buffer.
    @return true in case font is embeddable; false otherwise.
*/
bool FontEngineFT::canEmbed(const void* buffer, const uint32_t bufferLength)
{
#if defined(SK_BUILD_FOR_MAC) || defined(ANDROID)
    return false;
#else
    FT_Library  library;
    bool        retVal = false;

    assert(buffer && bufferLength);

    if (buffer && bufferLength) {
        FT_Open_Args  args;
        FT_Face       face;

        retVal = FT_Init_FreeType(&library);
        if (retVal) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            memset(&args, 0, sizeof(args));

            args.flags = FT_OPEN_MEMORY;
            args.memory_base = (const FT_Byte*)buffer;
            args.memory_size = bufferLength;

            if (FT_Open_Face(library, &args, 0, &face)) {
                FT_LOG("failed to create FT_Face\n");
            } else {
#ifdef FT_FSTYPE_RESTRICTED_LICENSE_EMBEDDING
                FT_UShort  fsType = FT_Get_FSType_Flags(face);
                retVal = ((fsType & (FT_FSTYPE_RESTRICTED_LICENSE_EMBEDDING | FT_FSTYPE_BITMAP_EMBEDDING_ONLY)) == 0) ? true : false;
#else
                // No embedding is 0x2 and bitmap embedding only is 0x200.
                TT_OS2* os2_table;
                if ((os2_table = (TT_OS2*)FT_Get_Sfnt_Table(face, ft_sfnt_os2)) != NULL) {
                    retVal = ((os2_table->fsType & 0x202) == 0) ? true : false;
                }/* end if */
#endif
                FT_LOG("canEmbed: %s\n", retVal ? "yes" : "no");

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
#endif
}/* end method canEmbed */

/** Given system path of the font file; returns the unhinted advances
	in font units.
	@param path              The system path to font file.
	@param start             The first glyph index.
	@param count             The number of glyph name values you want to
                                 retrieve.
	@param pGlyphsAdvance    The advances, in font units. This array must
                                 contain at least 'count' elements.
	@return 0 on success; return non-zero in case of error.
*/
uint32_t FontEngineFT::getGlyphsAdvance(const char path[], uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance)
{
    FT_Library  library;
    uint32_t    retVal = 0;

    assert(path && pGlyphsAdvance);

    if (path && pGlyphsAdvance) {
        FT_Face  face;

        retVal = FT_Init_FreeType(&library);
        if (retVal) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            retVal = FT_New_Face(library, path, 0, &face);
            if (retVal) {
                FT_LOG("failed to create FT_Face\n");
            } else {
#ifdef FT_ADVANCES_H
                retVal = FT_Get_Advances(face, start, count, FT_LOAD_NO_SCALE, (FT_Fixed*)pGlyphsAdvance);
#else
                if (!face || start >= face->num_glyphs ||
                        start + count > face->num_glyphs) {
                    retVal = 6;  // "Invalid argument."
                } else {
                    for (uint32_t i = 0; i < count; i++) {
                        FT_Error err = FT_Load_Glyph(face, i + start, FT_LOAD_NO_SCALE);
                        if (err) {
                            retVal = err;
                            break;
                        }/* end if */
                        pGlyphsAdvance[i] = face->glyph->advance.x;
                    }/* end for */
                }/* end else if */
#endif

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
}/* end method getGlyphsAdvance */

/** Given font data in buffer; returns the number of font units per em.
	@param buffer            The font file buffer.
	@param bufferLength      Length of the buffer.
	@param start             The first glyph index.
	@param count             The number of glyph name values you want to
                                 retrieve.
	@param pGlyphsAdvance    The advances, in font units. This array must
                                 contain at least 'count' elements.
	@return 0 on success; return non-zero in case of error.
*/
uint32_t FontEngineFT::getGlyphsAdvance(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance)
{
    FT_Library  library;
    uint32_t    retVal = 0;

    assert(buffer && bufferLength && pGlyphsAdvance);

    if (buffer && bufferLength && pGlyphsAdvance) {
        FT_Open_Args  args;
        FT_Face       face;

        retVal = FT_Init_FreeType(&library);
        if (retVal) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            memset(&args, 0, sizeof(args));

            args.flags = FT_OPEN_MEMORY;
            args.memory_base = (const FT_Byte*)buffer;
            args.memory_size = bufferLength;

            retVal = FT_Open_Face(library, &args, 0, &face);
            if (retVal) {
                FT_LOG("failed to create FT_Face\n");
            } else {
#ifdef FT_ADVANCES_H
                retVal = FT_Get_Advances(face, start, count, FT_LOAD_NO_SCALE, (FT_Fixed*)pGlyphsAdvance);
#else
                if (!face || start >= face->num_glyphs ||
                        start + count > face->num_glyphs) {
                    retVal = 6;  // "Invalid argument."
                } else {
                    for (uint32_t i = 0; i < count; i++) {
                        FT_Error err = FT_Load_Glyph(face, i + start, FT_LOAD_NO_SCALE);
                        if (err) {
                            retVal = err;
                            break;
                        }/* end if */
                        pGlyphsAdvance[i] = face->glyph->advance.x;
                    }/* end for */
                }/* end else if */
#endif

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
}/* end method getGlyphsAdvance */

/** Given system path of the font file; returns the glyph names.
	@param path           The system path to font file.
	@param start          The first glyph index.
	@param count          The number of glyph name values you want to
                              retrieve.
	@param pGlyphsName    The glyph names. This array must contain at
                              least 'count' elements.
	@return 0 on success; return non-zero in case of error.
*/
uint32_t FontEngineFT::getGlyphsName(const char path[], uint32_t start, uint32_t count, char** pGlyphsName)
{
    FT_Library  library;
    uint32_t    retVal = 0;

    assert(path && pGlyphsName);

    if (path && pGlyphsName) {
        FT_Face  face;

        retVal = FT_Init_FreeType(&library);
        if (retVal) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            retVal = FT_New_Face(library, path, 0, &face);
            if (retVal) {
                FT_LOG("failed to create FT_Face\n");
            } else {
                for (uint32_t i = 0; i < count; i++) {
                    FT_Error err = FT_Get_Glyph_Name(face, i + start, pGlyphsName[i], 128);
                    if (err) {
                        retVal = err;
                        break;
                    }/* end if */
                }/* end for */

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
}/* end method getGlyphsName */

/** Given font data in buffer; returns the glyph names.
	@param buffer          The font file buffer.
	@param bufferLength    Length of the buffer.
	@param start           The first glyph index.
	@param count           The number of glyph name values you want to
                               retrieve.
	@param pGlyphsName     The glyph names. This array must contain at
                               least 'count' elements.
	@return 0 on success; return non-zero in case of error.
*/
uint32_t FontEngineFT::getGlyphsName(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, char** pGlyphsName)
{
    FT_Library  library;
    uint32_t    retVal = 0;

    assert(buffer && bufferLength && pGlyphsName);

    if (buffer && bufferLength && pGlyphsName) {
        FT_Open_Args  args;
        FT_Face       face;

        retVal = FT_Init_FreeType(&library);
        if (retVal) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            memset(&args, 0, sizeof(args));

            args.flags = FT_OPEN_MEMORY;
            args.memory_base = (const FT_Byte*)buffer;
            args.memory_size = bufferLength;

            retVal = FT_Open_Face(library, &args, 0, &face);
            if (retVal) {
                FT_LOG("failed to create FT_Face\n");
            } else {
                for (uint32_t i = 0; i < count; i++) {
                    FT_Error err = FT_Get_Glyph_Name(face, i + start, pGlyphsName[i], 128);
                    if (err) {
                        retVal = err;
                        break;
                    }/* end if */
                }/* end for */

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
}/* end method getGlyphsName */

/** Given system path of the font file; returns the glyph unicodes.
	@param path              The system path to font file.
	@param start             The first glyph index.
	@param count             The number of unicode values you want to retrieve.
	@param pGlyphsUnicode    The glyph unicodes. This array must contain at least
                                 'count' elements.
	@return 0 on success; return non-zero in case of error.
*/
uint32_t FontEngineFT::getGlyphsUnicode(const char path[], uint32_t start, uint32_t count, int32_t* pGlyphsUnicode)
{
    FT_Library  library;
    uint32_t    retVal = 0;

    assert(path && pGlyphsUnicode);

    if (path && pGlyphsUnicode) {
        FT_Face  face;

        retVal = FT_Init_FreeType(&library);
        if (retVal) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            retVal = FT_New_Face(library, path, 0, &face);
            if (retVal) {
                FT_LOG("failed to create FT_Face\n");
            } else {
                // Check and see if we have Unicode cmaps.
                for (int i = 0; i < face->num_charmaps; ++i) {
                    // CMaps known to support Unicode:
                    // Platform ID   Encoding ID   Name
                    // -----------   -----------   -----------------------------------
                    // 0             0,1           Apple Unicode
                    // 0             3             Apple Unicode 2.0 (preferred)
                    // 3             1             Microsoft Unicode UCS-2
                    // 3             10            Microsoft Unicode UCS-4 (preferred)
                    //
                    // See Apple TrueType Reference Manual
                    // http://developer.apple.com/fonts/TTRefMan/RM06/Chap6cmap.html
                    // http://developer.apple.com/fonts/TTRefMan/RM06/Chap6name.html#ID
                    // Microsoft OpenType Specification
                    // http://www.microsoft.com/typography/otspec/cmap.htm

                    FT_UShort platformId = face->charmaps[i]->platform_id;
                    FT_UShort encodingId = face->charmaps[i]->encoding_id;

                    if (platformId != 0 && platformId != 3) {
                        continue;
                    }/* end if */

                    if (platformId == 3 && encodingId != 1 && encodingId != 10) {
                        continue;
                    }/* end if */

                    bool preferredMap = ((platformId == 3 && encodingId == 10) ||
                                                (platformId == 0 && encodingId == 3));

                    FT_Set_Charmap(face, face->charmaps[i]);

                    // Iterate through each cmap entry.
                    FT_UInt glyphIndex;
                    for (int32_t charCode = FT_Get_First_Char(face, &glyphIndex);
                            glyphIndex != 0;
                            charCode = FT_Get_Next_Char(face, charCode, &glyphIndex)) {
                        if ((glyphIndex >= start) &&  (glyphIndex <= (start + count - 1)))
                        {
                            if (charCode && (pGlyphsUnicode[glyphIndex - start] == 0 || preferredMap)) {
                                pGlyphsUnicode[glyphIndex - start] = charCode;
                            }/* end if */
                        }/* end if */
                    }/* end if */
                }/* end for */

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
}/* end method getGlyphsUnicode */

/** Given font data in buffer; returns the glyph unicodes.
	@param buffer            The font file buffer.
	@param bufferLength      Length of the buffer.
	@param start             The first glyph index.
	@param count             The number of unicode values you want to
                                 retrieve.
	@param pGlyphsUnicode    The glyph unicodes. This array must contain at least
                                 'count' elements.
	@return 0 on success; return non-zero in case of error.
*/
uint32_t FontEngineFT::getGlyphsUnicode(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, int32_t* pGlyphsUnicode)
{
    FT_Library  library;
    uint32_t    retVal = 0;

    assert(buffer && bufferLength && pGlyphsUnicode);

    if (buffer && bufferLength && pGlyphsUnicode) {
        FT_Open_Args  args;
        FT_Face       face;

        retVal = FT_Init_FreeType(&library);
        if (retVal) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            memset(&args, 0, sizeof(args));

            args.flags = FT_OPEN_MEMORY;
            args.memory_base = (const FT_Byte*)buffer;
            args.memory_size = bufferLength;

            retVal = FT_Open_Face(library, &args, 0, &face);
            if (retVal) {
                FT_LOG("failed to create FT_Face\n");
            } else {
                // Check and see if we have Unicode cmaps.
                for (int i = 0; i < face->num_charmaps; ++i) {
                    // CMaps known to support Unicode:
                    // Platform ID   Encoding ID   Name
                    // -----------   -----------   -----------------------------------
                    // 0             0,1           Apple Unicode
                    // 0             3             Apple Unicode 2.0 (preferred)
                    // 3             1             Microsoft Unicode UCS-2
                    // 3             10            Microsoft Unicode UCS-4 (preferred)
                    //
                    // See Apple TrueType Reference Manual
                    // http://developer.apple.com/fonts/TTRefMan/RM06/Chap6cmap.html
                    // http://developer.apple.com/fonts/TTRefMan/RM06/Chap6name.html#ID
                    // Microsoft OpenType Specification
                    // http://www.microsoft.com/typography/otspec/cmap.htm

                    FT_UShort platformId = face->charmaps[i]->platform_id;
                    FT_UShort encodingId = face->charmaps[i]->encoding_id;

                    if (platformId != 0 && platformId != 3) {
                        continue;
                    }/* end if */

                    if (platformId == 3 && encodingId != 1 && encodingId != 10) {
                        continue;
                    }/* end if */

                    bool preferredMap = ((platformId == 3 && encodingId == 10) ||
                                                (platformId == 0 && encodingId == 3));

                    FT_Set_Charmap(face, face->charmaps[i]);

                    // Iterate through each cmap entry.
                    FT_UInt glyphIndex;
                    for (int32_t charCode = FT_Get_First_Char(face, &glyphIndex);
                            glyphIndex != 0;
                            charCode = FT_Get_Next_Char(face, charCode, &glyphIndex)) {
                        if ((glyphIndex >= start) &&  (glyphIndex <= (start + count - 1)))
                        {
                            if (charCode && (pGlyphsUnicode[glyphIndex - start] == 0 || preferredMap)) {
                                pGlyphsUnicode[glyphIndex - start] = charCode;
                            }/* end if */
                        }/* end if */
                    }/* end if */
                }/* end for */

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return retVal;
}/* end method getGlyphsName */

static bool GetLetterCBox(FT_Face face, char letter, FT_BBox* bbox) {
    const FT_UInt glyph_id = FT_Get_Char_Index(face, letter);
    if (!glyph_id)
        return false;
    FT_Load_Glyph(face, glyph_id, FT_LOAD_NO_SCALE);
    FT_Outline_Get_CBox(&face->glyph->outline, bbox);
    return true;
}/* end method GetLetterCBox */

/** Retrieve detailed typeface metrics. Used by the PDF backend.
	@param path    The system path to font file.
	@return A pointer to vaild object on success; NULL is returned if
	the font is not found.
*/
AdvancedTypefaceMetrics* FontEngineFT::getAdvancedTypefaceMetrics(const char path[])
{
#if defined(SK_BUILD_FOR_MAC) || defined(ANDROID)
    return NULL;
#else
    FT_Library               library;
    AdvancedTypefaceMetrics*  pAdvancedTypefaceMetricsObj = NULL;

    assert(path);

    if (path) {
        FT_Face  face;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            if (FT_New_Face(library, path, 0, &face)) {
                FT_LOG("failed to create FT_Face\n");
            } else {
                pAdvancedTypefaceMetricsObj = new AdvancedTypefaceMetrics;

                pAdvancedTypefaceMetricsObj->pFontName = strdup(FT_Get_Postscript_Name(face));
                pAdvancedTypefaceMetricsObj->isMultiMaster = FT_HAS_MULTIPLE_MASTERS(face) ? true : false;
                pAdvancedTypefaceMetricsObj->fNumGlyphs = face->num_glyphs;
                pAdvancedTypefaceMetricsObj->fNumCharmaps = face->num_charmaps;
                pAdvancedTypefaceMetricsObj->fEmSize = 1000;

                bool cid = false;
                const char* fontType = FT_Get_X11_Font_Format(face);
                if (strcmp(fontType, "Type 1") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::TYPE1_FONT;
                } else if (strcmp(fontType, "CID Type 1") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::TYPE1CID_FONT;
                    cid = true;
                } else if (strcmp(fontType, "CFF") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::CFF_FONT;
                } else if (strcmp(fontType, "TrueType") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::TRUETYPE_FONT;
                    cid = true;
                    TT_Header* ttHeader;
                    if ((ttHeader = (TT_Header*)FT_Get_Sfnt_Table(face, ft_sfnt_head)) != NULL) {
                        pAdvancedTypefaceMetricsObj->fEmSize = ttHeader->Units_Per_EM;
                    }/* end if */
                }/* end else if */

                pAdvancedTypefaceMetricsObj->fStyle = 0;
                if (FT_IS_FIXED_WIDTH(face)) {
                    pAdvancedTypefaceMetricsObj->fStyle |= fem::FIXEDPITCH_STYLE;
                }/* end if */
                if (face->style_flags & FT_STYLE_FLAG_ITALIC) {
                    pAdvancedTypefaceMetricsObj->fStyle |= fem::ITALIC_STYLE;
                }/* end if */
                // We should set either Symbolic or Nonsymbolic; Nonsymbolic if the font's
                // character set is a subset of 'Adobe standard Latin.'
                pAdvancedTypefaceMetricsObj->fStyle |= fem::SYMBOLIC_STYLE;

                PS_FontInfoRec ps_info;
                TT_Postscript* tt_info;
                if (FT_Get_PS_Font_Info(face, &ps_info) == 0) {
                    pAdvancedTypefaceMetricsObj->fItalicAngle = (int16_t)ps_info.italic_angle;
                } else if ((tt_info = (TT_Postscript*)FT_Get_Sfnt_Table(face, ft_sfnt_post)) != NULL) {
                    pAdvancedTypefaceMetricsObj->fItalicAngle = (int16_t)(tt_info->italicAngle >> 16);
                } else {
                    pAdvancedTypefaceMetricsObj->fItalicAngle = 0;
                }/* end else if */

                pAdvancedTypefaceMetricsObj->fAscent = face->ascender;
                pAdvancedTypefaceMetricsObj->fDescent = face->descender;

                // Figure out a good guess for StemV - Min width of i, I, !, 1.
                // This probably isn't very good with an italic font.
                int16_t min_width = SHRT_MAX;
                pAdvancedTypefaceMetricsObj->fStemV = 0;
                char stem_chars[] = {'i', 'I', '!', '1'};
                for (size_t i = 0; i < sizeof(stem_chars)/sizeof(stem_chars[0]); i++) {
                    FT_BBox bbox;
                    if (GetLetterCBox(face, stem_chars[i], &bbox)) {
                        int16_t width = bbox.xMax - bbox.xMin;
                        if (width > 0 && width < min_width) {
                            min_width = width;
                            pAdvancedTypefaceMetricsObj->fStemV = min_width;
                        }/* end if */
                    }/* end if */
                }/* end for */

                TT_PCLT* pclt_info;
                TT_OS2* os2_table;
                if ((pclt_info = (TT_PCLT*)FT_Get_Sfnt_Table(face, ft_sfnt_pclt)) != NULL) {
                    pAdvancedTypefaceMetricsObj->fCapHeight = pclt_info->CapHeight;
                    uint8_t serif_style = pclt_info->SerifStyle & 0x3F;
                    if (serif_style >= 2 && serif_style <= 6) {
                        pAdvancedTypefaceMetricsObj->fStyle |= fem::SERIF_STYLE;
                    } else if (serif_style >= 9 && serif_style <= 12) {
                        pAdvancedTypefaceMetricsObj->fStyle |= fem::SCRIPT_STYLE;
                    }/* end else if */
                } else if ((os2_table = (TT_OS2*)FT_Get_Sfnt_Table(face, ft_sfnt_os2)) != NULL) {
                    pAdvancedTypefaceMetricsObj->fCapHeight = os2_table->sCapHeight;
                } else {
                    // Figure out a good guess for CapHeight: average the height of M and X.
                    FT_BBox m_bbox, x_bbox;
                    bool got_m, got_x;
                    got_m = GetLetterCBox(face, 'M', &m_bbox);
                    got_x = GetLetterCBox(face, 'X', &x_bbox);
                    if (got_m && got_x) {
                        pAdvancedTypefaceMetricsObj->fCapHeight = (m_bbox.yMax - m_bbox.yMin + x_bbox.yMax - x_bbox.yMin) / 2;
                    } else if (got_m && !got_x) {
                        pAdvancedTypefaceMetricsObj->fCapHeight = m_bbox.yMax - m_bbox.yMin;
                    } else if (!got_m && got_x) {
                        pAdvancedTypefaceMetricsObj->fCapHeight = x_bbox.yMax - x_bbox.yMin;
                    }/* end else if */
                }/* end else if */

                pAdvancedTypefaceMetricsObj->fMaxAdvWidth = face->max_advance_width;

                pAdvancedTypefaceMetricsObj->fXMin = face->bbox.xMin;
                pAdvancedTypefaceMetricsObj->fYMin = face->bbox.yMin;
                pAdvancedTypefaceMetricsObj->fXMax = face->bbox.xMax;
                pAdvancedTypefaceMetricsObj->fYMax = face->bbox.yMax;

                pAdvancedTypefaceMetricsObj->isScalable = FT_IS_SCALABLE(face) ? true : false;
                pAdvancedTypefaceMetricsObj->hasVerticalMetrics = FT_HAS_VERTICAL(face) ? true : false;

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return pAdvancedTypefaceMetricsObj;
#endif
}/* end method getAdvancedTypefaceMetrics */

/** Retrieve detailed typeface metrics. Used by the PDF backend.
	@param buffer          The font file buffer.
	@param bufferLength    Length of the buffer.
	@return A pointer to vaild object on success; NULL is returned if
	the font is not found.
*/
AdvancedTypefaceMetrics* FontEngineFT::getAdvancedTypefaceMetrics(const void* buffer, const uint32_t bufferLength)
{
#if defined(SK_BUILD_FOR_MAC) || defined(ANDROID)
    return NULL;
#else
    FT_Library  library;
    AdvancedTypefaceMetrics*  pAdvancedTypefaceMetricsObj = NULL;

    assert(buffer && bufferLength);

    if (buffer && bufferLength) {
        FT_Open_Args  args;
        FT_Face       face;

        if (FT_Init_FreeType(&library)) {
            FT_LOG("failed to initalized FreeType\n");
        } else {
            memset(&args, 0, sizeof(args));

            args.flags = FT_OPEN_MEMORY;
            args.memory_base = (const FT_Byte*)buffer;
            args.memory_size = bufferLength;

            if (FT_Open_Face(library, &args, 0, &face)) {
                FT_LOG("failed to create FT_Face\n");
            } else {
                pAdvancedTypefaceMetricsObj = new AdvancedTypefaceMetrics;

                pAdvancedTypefaceMetricsObj->pFontName = strdup(FT_Get_Postscript_Name(face));
                pAdvancedTypefaceMetricsObj->isMultiMaster = FT_HAS_MULTIPLE_MASTERS(face) ? true : false;
                pAdvancedTypefaceMetricsObj->fNumGlyphs = face->num_glyphs;
                pAdvancedTypefaceMetricsObj->fNumCharmaps = face->num_charmaps;
                pAdvancedTypefaceMetricsObj->fEmSize = 1000;

                bool cid = false;
                const char* fontType = FT_Get_X11_Font_Format(face);
                if (strcmp(fontType, "Type 1") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::TYPE1_FONT;
                } else if (strcmp(fontType, "CID Type 1") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::TYPE1CID_FONT;
                    cid = true;
                } else if (strcmp(fontType, "CFF") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::CFF_FONT;
                } else if (strcmp(fontType, "TrueType") == 0) {
                    pAdvancedTypefaceMetricsObj->fType = fem::TRUETYPE_FONT;
                    cid = true;
                    TT_Header* ttHeader;
                    if ((ttHeader = (TT_Header*)FT_Get_Sfnt_Table(face, ft_sfnt_head)) != NULL) {
                        pAdvancedTypefaceMetricsObj->fEmSize = ttHeader->Units_Per_EM;
                    }/* end if */
                }/* end else if */

                pAdvancedTypefaceMetricsObj->fStyle = 0;
                if (FT_IS_FIXED_WIDTH(face)) {
                    pAdvancedTypefaceMetricsObj->fStyle |= fem::FIXEDPITCH_STYLE;
                }/* end if */
                if (face->style_flags & FT_STYLE_FLAG_ITALIC) {
                    pAdvancedTypefaceMetricsObj->fStyle |= fem::ITALIC_STYLE;
                }/* end if */
                // We should set either Symbolic or Nonsymbolic; Nonsymbolic if the font's
                // character set is a subset of 'Adobe standard Latin.'
                pAdvancedTypefaceMetricsObj->fStyle |= fem::SYMBOLIC_STYLE;

                PS_FontInfoRec ps_info;
                TT_Postscript* tt_info;
                if (FT_Get_PS_Font_Info(face, &ps_info) == 0) {
                    pAdvancedTypefaceMetricsObj->fItalicAngle = (int16_t)ps_info.italic_angle;
                } else if ((tt_info = (TT_Postscript*)FT_Get_Sfnt_Table(face, ft_sfnt_post)) != NULL) {
                    pAdvancedTypefaceMetricsObj->fItalicAngle = (int16_t)(tt_info->italicAngle >> 16);
                } else {
                    pAdvancedTypefaceMetricsObj->fItalicAngle = 0;
                }/* end else if */

                pAdvancedTypefaceMetricsObj->fAscent = face->ascender;
                pAdvancedTypefaceMetricsObj->fDescent = face->descender;

                // Figure out a good guess for StemV - Min width of i, I, !, 1.
                // This probably isn't very good with an italic font.
                int16_t min_width = SHRT_MAX;
                pAdvancedTypefaceMetricsObj->fStemV = 0;
                char stem_chars[] = {'i', 'I', '!', '1'};
                for (size_t i = 0; i < sizeof(stem_chars)/sizeof(stem_chars[0]); i++) {
                    FT_BBox bbox;
                    if (GetLetterCBox(face, stem_chars[i], &bbox)) {
                        int16_t width = bbox.xMax - bbox.xMin;
                        if (width > 0 && width < min_width) {
                            min_width = width;
                            pAdvancedTypefaceMetricsObj->fStemV = min_width;
                        }/* end if */
                    }/* end if */
                }/* end for */

                TT_PCLT* pclt_info;
                TT_OS2* os2_table;
                if ((pclt_info = (TT_PCLT*)FT_Get_Sfnt_Table(face, ft_sfnt_pclt)) != NULL) {
                    pAdvancedTypefaceMetricsObj->fCapHeight = pclt_info->CapHeight;
                    uint8_t serif_style = pclt_info->SerifStyle & 0x3F;
                    if (serif_style >= 2 && serif_style <= 6) {
                        pAdvancedTypefaceMetricsObj->fStyle |= fem::SERIF_STYLE;
                    } else if (serif_style >= 9 && serif_style <= 12) {
                        pAdvancedTypefaceMetricsObj->fStyle |= fem::SCRIPT_STYLE;
                    }/* end else if */
                } else if ((os2_table = (TT_OS2*)FT_Get_Sfnt_Table(face, ft_sfnt_os2)) != NULL) {
                    pAdvancedTypefaceMetricsObj->fCapHeight = os2_table->sCapHeight;
                } else {
                    // Figure out a good guess for CapHeight: average the height of M and X.
                    FT_BBox m_bbox, x_bbox;
                    bool got_m, got_x;
                    got_m = GetLetterCBox(face, 'M', &m_bbox);
                    got_x = GetLetterCBox(face, 'X', &x_bbox);
                    if (got_m && got_x) {
                        pAdvancedTypefaceMetricsObj->fCapHeight = (m_bbox.yMax - m_bbox.yMin + x_bbox.yMax - x_bbox.yMin) / 2;
                    } else if (got_m && !got_x) {
                        pAdvancedTypefaceMetricsObj->fCapHeight = m_bbox.yMax - m_bbox.yMin;
                    } else if (!got_m && got_x) {
                        pAdvancedTypefaceMetricsObj->fCapHeight = x_bbox.yMax - x_bbox.yMin;
                    }/* end else if */
                }/* end else if */

                pAdvancedTypefaceMetricsObj->fMaxAdvWidth = face->max_advance_width;

                pAdvancedTypefaceMetricsObj->fXMin = face->bbox.xMin;
                pAdvancedTypefaceMetricsObj->fYMin = face->bbox.yMin;
                pAdvancedTypefaceMetricsObj->fXMax = face->bbox.xMax;
                pAdvancedTypefaceMetricsObj->fYMax = face->bbox.yMax;

                pAdvancedTypefaceMetricsObj->isScalable = FT_IS_SCALABLE(face) ? true : false;
                pAdvancedTypefaceMetricsObj->hasVerticalMetrics = FT_HAS_VERTICAL(face) ? true : false;

                FT_Done_Face(face);
            }/* end else if */

            FT_Done_FreeType(library);
        }/* end else if */
    }/* end if */

    return pAdvancedTypefaceMetricsObj;
#endif
}/* end method getAdvancedTypefaceMetrics */

FontScaler* FontEngineFT::createFontScalerContext(const FontScalerInfo& desc)
{
    android::Mutex::Autolock ac(gMutexFT);
    return this->getFontScaler(desc);
}/* end method createFontScalerContext */

FontScaler* FontEngineFT::getFontScaler(const FontScalerInfo& desc)
{
    FontScaler*  pFontScaler = NULL;

    FontFT*  pFont = this->getFont(desc);
    if (pFont == NULL) {
        pFont = new FontFT(desc);
        if (! pFont->success()) {
            delete pFont;

            if (gCountFontFT == 0) {
                /* required as font was not initialized */
                FT_LOG("FT_Done_FreeType\n");
                FT_Done_FreeType(gLibraryFT);

                assert(gLibraryFT = NULL);
            }/* end if */

            return NULL;
        }/* end if */

        FontNodePtr fontNode = (FontNodePtr)malloc(sizeof(FontNode));
        assert(fontNode != NULL);
        if (fontNode == NULL) {
            FT_LOG("malloc failed to allocate memory for FontNode instance\n");
            delete pFont;
            return NULL;
        }/* end if */

        fontNode->next = NULL;
        fontNode->font = pFont;

        pFontScaler = pFont->getFontScaler(desc);
        if (pFontScaler == NULL) {
            free(fontNode);
            return NULL;
        }/* end if */

        FontFTAddAtHead(&this->pFontList, fontNode);
    } else {
        pFontScaler = pFont->getFontScaler(desc);
    }/* end else if */

    return pFontScaler;
}/* end method getFontScaler */

FontFT* FontEngineFT::getFont(const FontScalerInfo& desc)
{
    FontNodePtr node = this->pFontList;
    FontFT* pFontFT = NULL;

    while (node != NULL) {
        if (desc.pBuffer && node->font->pBuffer) {
            FT_LOG("[%x:%x]->", node, node->font->pBuffer);
            if ( desc.pBuffer == node->font->pBuffer ) {
                pFontFT = node->font;
                break;
            }/* end if */
        } else {
            FT_LOG("[%x:%s]->", node, node->font->pPath);
            if ( ! strcmp(desc.pPath, node->font->pPath) ) {
                pFontFT = node->font;
                break;
            }/* end if */
        }/* end else if */
        node = node->next;
    }/* end while */

    return pFontFT;
}/* end method getFont */

#ifdef __cplusplus
extern "C" {
#endif
    static unsigned long ft_stream_read(FT_Stream       stream,
                                        unsigned long   offset,
                                        unsigned char*  buffer,
                                        unsigned long   count )
    {
        return streamRead((void*)stream->descriptor.pointer, offset, buffer, count);
    }/* end method ft_stream_read */

    static void ft_stream_close(FT_Stream  stream) { FT_UNUSED(stream); }/* end method ft_stream_close */
#ifdef __cplusplus
}/* end extern "C" */
#endif

FontFT::FontFT(const FontScalerInfo& desc)
    : pPath(NULL), bInitialized(false), refCnt(0)
{
    FT_Error    err;
    int flag = 0;

#ifdef ENABLE_FONTINSTLIST
    pFontInstList = NULL;
#endif /* ENABLE_FONTINSTLIST */

    if (gCountFontFT == 0) {
        if (! InitFreetype()) {
            assert(0);
        }/* end if */
    }/* end if */

    memset(&streamRecFT, 0, sizeof(FT_StreamRec));
    streamRecFT.size = desc.size;
    streamRecFT.descriptor.pointer = desc.pStream;
    streamRecFT.read  = ft_stream_read;
    streamRecFT.close = ft_stream_close;

    FT_Open_Args  args;
    memset(&args, 0, sizeof(args));

    fontID = desc.fontID;

    pBuffer = desc.pBuffer;
    if (pBuffer) {
        args.flags = FT_OPEN_MEMORY;
        args.memory_base = (const FT_Byte*)pBuffer;
        args.memory_size = desc.size;

        flag = 1;
        FT_LOG("flag : %d\n", flag);
    } else if (desc.pStream) {
        args.flags = FT_OPEN_STREAM;
        args.stream = &streamRecFT;

        flag = 2;
        FT_LOG("flag : %d\n", flag);
    }/* end else if */

    if (flag) {
        err = FT_Open_Face(gLibraryFT, &args, 0, &pFace);
    } else {
        err = FT_New_Face(gLibraryFT, desc.pPath, 0, &pFace);
    }/* end else if */

    if (err) {
        FT_LOG("unable to create FT_Face for font '%d', error num : '%d' \n", fontID, err);
        return;
    } else {
        if (desc.pPath) {
            pPath = strdup(desc.pPath);
        }/* end if */

        ++gCountFontFT;
        bInitialized = true;
    }/* end else if */
}/* end constructor FontFT */

FontScaler* FontFT::getFontScaler(const FontScalerInfo& desc)
{
    FontScaler* pFontScaler = NULL;
    FontInstFT* fontInst = NULL;

#ifdef ENABLE_FONTINSTLIST
    FontInstNodePtr fontInstNode = NULL;
#endif /* ENABLE_FONTINSTLIST */

    FTEnginePrintList;

#ifndef ENABLE_FONTINSTLIST
    fontInst = new FontInstFT(desc, this);
    if (! fontInst->success()) {
        delete fontInst;
        return NULL;
    }/* end if */
#else
    fontInstNode = searchFontInst(desc);
    if (NULL == fontInstNode)
    {
        fontInst = new FontInstFT(desc, this);
        if (! fontInst->success()) {
            delete fontInst;
            return NULL;
        }/* end if */

        fontInstNode = (FontInstNodePtr)malloc(sizeof(FontInstNode));
        if (fontInstNode == NULL) {
            FT_LOG("malloc failed to allocate memory for FontInstNode\n");
            delete fontInst;
            return NULL;
        }/* end if */

        fontInstNode->inst = fontInst;
        fontInstNode->next = NULL;
        FontFTAddAtHead(&this->pFontInstList, fontInstNode);
        FT_LOG("Font: %x, Font instance: %x\n", fontInst, fontInstNode);
    } else {
        fontInst = fontInstNode->inst;
    }/* end else if */
#endif /* ENABLE_FONTINSTLIST */

    pFontScaler = new FontScalerFT(fontInst);
    FT_LOG("strike %x created\n", pFontScaler);

    return pFontScaler;
}/* end method getFontScaler */

#ifdef ENABLE_FONTINSTLIST
FontInstNodePtr FontFT::searchFontInst(const FontScalerInfo& desc)
{
  FT_Matrix  ftMatrix22;
  FT_Matrix  ftMatrix22Temp;
  FEM16Dot16 fScaleX;
  FEM16Dot16 fScaleY;
  uint32_t   loadGlyphFlags;

  FontInstNodePtr node = this->pFontInstList;
  FontInstFT* inst;

  this->getTransMatrix(desc, ftMatrix22, fScaleX, fScaleY, loadGlyphFlags);

  FT_LOG("FontScalerInfo -- fontID : %d, loadFlags : %d\n", desc.fontID, loadGlyphFlags);
  FT_LOG("FontScalerInfo -- xx  : %d, xy : %d, yx : %d, yy : %d, scaleX : %d, scaleY : %d\n", ftMatrix22.xx >> 16, ftMatrix22.xy >> 16, ftMatrix22.yx >> 16, ftMatrix22.yy >> 16, fScaleX >> 16, fScaleY >> 16);

  while(node)
  {
    inst = node->inst;
    ftMatrix22Temp = inst->ftMatrix22;
    if( (ftMatrix22Temp.xx == ftMatrix22.xx) &&
        (ftMatrix22Temp.xy == ftMatrix22.xy) &&
        (ftMatrix22Temp.yx == ftMatrix22.yx) &&
        (ftMatrix22Temp.yy == ftMatrix22.yy) &&
        (inst->fScaleX == fScaleX) &&
        (inst->fScaleY == fScaleY) &&
        (inst->loadGlyphFlags == loadGlyphFlags) &&
        (inst->fontInstFlags == desc.flags) )
    {
        FT_LOG("font instance -- fontID : %d, loadFlags : %d\n", fontID, inst->loadGlyphFlags);
        FT_LOG("font instance -- xx  : %d, xy : %d, yx : %d, yy : %d, scaleX : %d, scaleY : %d\n", ftMatrix22Temp.xx >> 16, ftMatrix22Temp.xy >> 16, ftMatrix22Temp.yx >> 16, ftMatrix22Temp.yy >> 16, inst->fScaleX >> 16, inst->fScaleY >> 16);

        FT_LOG("font instance found!!\n");

        return node;
    }/* end if */

    node = node->next;
  }/* end while */

  FT_LOG("could not found font instance!!\n");
  return NULL;
}/* end method searchFontInst */
#endif /* ENABLE_FONTINSTLIST */

void FontFT::getTransMatrix(const FontScalerInfo& desc, FT_Matrix& ftMatrix22, FEM16Dot16& fScaleX, FEM16Dot16& fScaleY, uint32_t& loadGlyphFlags)
{
    /* compute our scale factors */
    FEM16Dot16 sx = desc.fScaleX;
    FEM16Dot16 sy = desc.fScaleY;

    if (desc.fSkewX || desc.fSkewY || sx < 0 || sy < 0) {
        /* sort of give up on hinting */
        sx = FEM16Dot16Abs(sx) > FEM16Dot16Abs(desc.fSkewX) ? FEM16Dot16Abs(sx): FEM16Dot16Abs(desc.fSkewX);
        sy = FEM16Dot16Abs(desc.fSkewY) > FEM16Dot16Abs(sy) ? FEM16Dot16Abs(desc.fSkewY) : FEM16Dot16Abs(sy);
        sx = sy = FEM16Dot16Avg(sx, sy);

        FEM16Dot16 inv = FEM16Dot16Invert(sx);

        /* flip the skew elements to go from our Y-down system to FreeType's */
        ftMatrix22.xx = FT_MulFix(desc.fScaleX, inv);
        ftMatrix22.xy = -FT_MulFix(desc.fSkewX, inv);
        ftMatrix22.yx = -FT_MulFix(desc.fSkewY, inv);
        ftMatrix22.yy = FT_MulFix(desc.fScaleY, inv);
    } else {
        ftMatrix22.xx = ftMatrix22.yy = FEMOne16Dot16;
        ftMatrix22.xy = ftMatrix22.yx = 0;
    }

    fScaleX = sx;
    fScaleY = sy;

    /* compute the flags we send to FT_Load_Glyph */
    {
        FT_Int32 loadFlags = FT_LOAD_DEFAULT;
        fem::Hinting h = static_cast<fem::Hinting>((desc.flags & fem::Hinting_Flag) >> 1);

        if (desc.subpixelPositioning) {
            switch (h) {
                case fem::HINTING_NONE:
                    loadFlags = FT_LOAD_NO_HINTING;
                    FT_LOG("subpixel positioning; hinting none, setting loadFlags to no hinting\n");
                    break;
                case fem::HINTING_FULL:
                    loadFlags = FT_LOAD_TARGET_NORMAL;
                    FT_LOG("subpixel positioning; hinting full, setting loadFlags to normal hinting\n");
                    if ( gLCDSupport ) {
                        if (fem::ALIAS_LCD_H == desc.maskFormat) {
                            loadFlags = FT_LOAD_TARGET_LCD;
                        } else if (fem::ALIAS_LCD_V == desc.maskFormat) {
                            loadFlags = FT_LOAD_TARGET_LCD_V;
                        }/* end else if */
                    }/* end if */
                    break;
                default :
                    /* HINTING_LIGHT or HINTING_NORMAL */
                    loadFlags = FT_LOAD_TARGET_LIGHT;  // This implies FORCE_AUTOHINT
                    FT_LOG("subpixel positioning; hinting light/normal, setting loadFlags to light hinting\n");
            }/* end switch */
        } else {
            switch (h) {
                case fem::HINTING_NONE:
                    loadFlags = FT_LOAD_NO_HINTING;
                    FT_LOG("hinting none, setting loadFlags to no hinting\n");
                    break;
                case fem::HINTING_NORMAL:
                case fem::HINTING_FULL:
                    loadFlags = FT_LOAD_TARGET_NORMAL;
                    FT_LOG("hinting normal/full, setting loadFlags to normal hinting\n");
                    if ( gLCDSupport ) {
                         if (fem::ALIAS_LCD_H == desc.maskFormat) {
                            loadFlags = FT_LOAD_TARGET_LCD;
                         } else if (fem::ALIAS_LCD_V == desc.maskFormat) {
                            loadFlags = FT_LOAD_TARGET_LCD_V;
                         }/* end else if  */
                    }/* end if*/
                    break;
                default :
                    /* HINTING_LIGHT */
                    loadFlags = FT_LOAD_TARGET_LIGHT;  // This implies FORCE_AUTOHINT
                    FT_LOG("hinting light, setting loadFlags to light hinting\n");
            }/* end switch */
        }/* end else if */

        if ((desc.flags & fem::EmbeddedBitmapText_Flag) == 0) {
            FT_LOG("setting loadFlags to do not load the embedded bitmaps of scalable formats\n");
            loadFlags |= FT_LOAD_NO_BITMAP;
        }/* end if */

        loadGlyphFlags = loadFlags;
    }
}/* end method getTransMatrix */

FontFT::~FontFT()
{
    if (this->bInitialized) {
        FontNodePtr curr = gFontEngineInstFT->pFontList;
        FontNodePtr prev = NULL;
        FontNodePtr next = NULL;

        while (curr) {
            next = curr->next;
            if (curr->font == this) {
                if (prev) {
                    prev->next = next;
                } else {
                    gFontEngineInstFT->pFontList = next;
                }/* end else if */

                free(curr);
                FT_LOG("deleted font node corresponding to font : %s from font list\n", this->pPath);
                break;
            }/* end if */
            prev = curr;
            curr = next;
        }/* end while */

        if ( pPath ) {
            free((char*)pPath);
        }/* end if */

        FT_Done_Face(pFace);
        pFace = NULL;

        if (--gCountFontFT == 0) {
            FT_LOG("FT_Done_FreeType\n");
            FT_Done_FreeType(gLibraryFT);
            assert(gLibraryFT = NULL);
        }/* end if */
    }/* end if */
}/* end destructor FontFT */

FontInstFT::FontInstFT(const FontScalerInfo& desc, FontFT* font)
    : ftSize( NULL), pFont(font), refCnt(0), bInitialized(false)
{
    pFont->getTransMatrix(desc, ftMatrix22, fScaleX, fScaleY, loadGlyphFlags);

    FT_LOG("getTransMatrix returned, xx  : %d, xy : %d, yx : %d, yy : %d, scaleX : %d, scaleY : %d\n",
              ftMatrix22.xx >> 16, ftMatrix22.xy >> 16, ftMatrix22.yx >> 16,
              ftMatrix22.yy >> 16, fScaleX >> 16, fScaleY >> 16);

    subpixelPositioning = desc.subpixelPositioning;
    fontInstFlags = desc.flags;
    maskFormat = desc.maskFormat;

    /* now create the FT_Size */
    {
        FT_Error    err;

        err = FT_New_Size(pFont->pFace, &ftSize);
        if (err != 0) {
            FT_LOG("FT_New_Size(%d): FT_Set_Char_Size(%x, %x) returned %x\n",
                      desc.fontID, fScaleX, fScaleY, err);
            return;
        }

        err = FT_Activate_Size(ftSize);
        if (err != 0) {
            FT_LOG("FT_Activate_Size(%d, %x, %x) returned %x\n",
                      desc.fontID, fScaleX, fScaleY, err);

            FT_Done_Size(ftSize);
            ftSize = NULL;

            return;
        }

        err = FT_Set_Char_Size(pFont->pFace,
                                  FEM16Dot16ToFEM26Dot6(fScaleX),
                                  FEM16Dot16ToFEM26Dot6(fScaleY),
                                  72, 72);
        if (err != 0) {
            FT_LOG("FT_Set_Char_Size(%d, %x, %x) returned %x\n",
                      desc.fontID, fScaleX, fScaleY, err);

            FT_Done_Size(ftSize);
            ftSize = NULL;

            return;
        }

        FT_Set_Transform(pFont->pFace, &ftMatrix22, NULL);
    }

    bInitialized = true;
    this->pFont->refCnt++;
}/* end constructor FontInstFT */

FontInstFT::~FontInstFT()
{
    if (bInitialized) {
#ifndef ENABLE_FONTINSTLIST
        FT_Done_Size(ftSize);
        ftSize = NULL;

        if( (-- this->pFont->refCnt) == 0 ) {
            delete this->pFont;
        }/* end if */

        FT_LOG("font instance %x destroyed\n", this);
#else
        FontInstNodePtr curr = this->pFont->pFontInstList;
        FontInstNodePtr prev = NULL;
        FontInstNodePtr next = NULL;

        while (curr) {
            next = curr->next;
            if (curr->inst == this) {
                if (prev) {
                    prev->next = next;
                } else {
                    this->pFont->pFontInstList = next;
                }/* end else if */

                free(curr);
                break;
            }/* end if */
            prev = curr;
            curr = next;
        }/* end while */

        FT_Done_Size(ftSize);
        ftSize = NULL;

        if( (-- this->pFont->refCnt) == 0 ) {
            delete this->pFont;
        }/* end if */

        FT_LOG("font instance %x destroyed\n", this);
#endif /* ENABLE_FONTINSTLIST */
    } else {
        if( (this->pFont->refCnt) == 0 ) {
            delete this->pFont;
        }/* end if */
    }/* end if */
}/* end destructor FontInstFT */

/*  We call this before each use of the fFace, since we may be sharing
    this face with other context (at different sizes).

    Return : 0 on success; non zero value otherwise.
*/
FT_Error FontInstFT::setupSize()
{
    FT_Error    err = 0;

    assert(bInitialized);

    FT_LOG("this : %x, xx  : %d, xy : %d, yx : %d, yy : %d, scaleX : %d, scaleY : %d\n", this, ftMatrix22.xx >> 16, ftMatrix22.xy >> 16, ftMatrix22.yx >> 16, ftMatrix22.yy >> 16, fScaleX >> 16, fScaleY >> 16);

    err = FT_Activate_Size(ftSize);
    if (err != 0) {
        FT_LOG("FT_Activate_Size(%s, %x, %x) returned %x\n",
                  pFont->pPath, fScaleX, fScaleY, err);
    } else {
        /* seems we need to reset this every time (not sure why, but without it
         * I get random italics from some other fFTSize) */
        FT_Set_Transform(pFont->pFace, &ftMatrix22, NULL);
    }/* end else if */

    FT_LOG("successfully set transformation for font instance: %x\n", this);
    return err;
}/* end method setupSize */

/**
 * FontScalerFT.
 */
FontScalerFT::FontScalerFT(FontInstFT* fontInst) : pFontInst(fontInst), bInitialized(false)
{
    assert(pFontInst);

    ftFace = fontInst->pFont->pFace;
    bInitialized = true;
    this->pFontInst->refCnt++;

    FT_LOG("strike %x created\n", this);
}/* end method constructor */

FontScalerFT::~FontScalerFT()
{
    android::Mutex::Autolock ac(gMutexFT);

    if (bInitialized) {
        if (ftFace) {
            ftFace = NULL;
        }/* end if */

        if( (-- this->pFontInst->refCnt) == 0 ) {
            /* delete font instance */
            delete this->pFontInst;
        }/* end if */
    }/* end if */

    FT_LOG("strike %x destroyed\n", this);
}/* end method destructor */

uint16_t FontScalerFT::getCharToGlyphID(int32_t charUniCode)
{
    android::Mutex::Autolock ac(gMutexFT);

    FT_LOG("unicode : %d, glyph : %d\n", charUniCode, (uint16_t)FT_Get_Char_Index(ftFace, (FT_ULong)charUniCode));
    return (uint16_t)FT_Get_Char_Index(ftFace, (FT_ULong)charUniCode);
}/* end method getCharToGlyphID */

int32_t FontScalerFT::getGlyphIDToChar(uint16_t glyphID)
{
    android::Mutex::Autolock ac(gMutexFT);

    /* iterate through each cmap entry, looking for matching glyph indices */
    FT_UInt glyphIndex;
    int32_t charCode = FT_Get_First_Char(ftFace, &glyphIndex);

    while (glyphIndex != 0) {
        if (glyphIndex == glyphID) {
            FT_LOG("glyph : %d, unicode : %d\n", glyphID, charCode);
            return charCode;
        }/* end if */
        charCode = FT_Get_Next_Char(ftFace, charCode, &glyphIndex);
    }/* end while */

    FT_LOG("glyph : %d, unicode : 0\n", glyphID);
    return 0;
}/* end method getCharToGlyphID */

uint16_t FontScalerFT::getGlyphCount() const
{
    return (uint16_t)ftFace->num_glyphs;
}/* end method getGlyphCount */

GlyphMetrics FontScalerFT::getGlyphAdvance(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY)
{
    GlyphMetrics  gm;
#ifdef FT_ADVANCES_H
    /* unhinted and light hinted text have linearly scaled advances
     * which are very cheap to compute with some font formats...
     */
    {
        android::Mutex::Autolock ac(gMutexFT);

        FT_UNUSED(fracX);
        FT_UNUSED(fracY);

        if (this->pFontInst->setupSize()) {
            return gm;
        }

        FT_Error  error;
        FT_Fixed  advance;

        error = FT_Get_Advance(ftFace, glyphID,
                                this->pFontInst->loadGlyphFlags | FT_ADVANCE_FLAG_FAST_ONLY,
                                &advance);
        if (0 == error) {
            gm.rsbDelta = 0;
            gm.lsbDelta = 0;
            gm.fAdvanceX = advance;  // advance *2/3; //DEBUG
            gm.fAdvanceY = 0;

            FT_LOG("glyph : %d, advanceX : %d, advanceY : %d\n", glyphID, gm.fAdvanceX >> 16, gm.fAdvanceY >> 16);
        }/* end if */
    }
#else
    /* otherwise, we need to load/hint the glyph, which is slower */
    gm  = this->getGlyphMetrics(glyphID, fracX, fracY);
#endif/* FT_ADVANCES_H */

    return gm;
}/* end method getGlyphAdvance */

GlyphMetrics FontScalerFT::getGlyphMetrics(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY)
{
    android::Mutex::Autolock ac(gMutexFT);
    GlyphMetrics  gm;

    FT_Error    err;

    if (this->pFontInst->setupSize()) {
        goto ERROR;
    }/* end if */

    err = FT_Load_Glyph(ftFace, glyphID, this->pFontInst->loadGlyphFlags);
    if (err != 0) {
        FT_LOG("FT_Load_Glyph(glyph:%d flags:%d) returned %x\n",
                 glyphID, this->pFontInst->loadGlyphFlags, err);
    ERROR:
        gm.clear(); /* or memset(&gm, 0, sizeof(GlyphMetrics)); */
        return gm;
    }/* end if */

    switch ( ftFace->glyph->format ) {
        case FT_GLYPH_FORMAT_OUTLINE: {
            FT_BBox bbox;

            if (this->pFontInst->fontInstFlags & fem::Embolden_Flag) {
                emboldenOutline(&ftFace->glyph->outline);
            }/* end if */

            FT_Outline_Get_CBox(&ftFace->glyph->outline, &bbox);

            if (this->pFontInst->subpixelPositioning) {
                int dx = fracX >> 10;
                int dy = fracY >> 10;

                /* negate dy since freetype-y-goes-up and skia-y-goes-down */
                bbox.xMin += dx;
                bbox.yMin -= dy;
                bbox.xMax += dx;
                bbox.yMax -= dy;
            }/* end if */

            bbox.xMin &= ~63;
            bbox.yMin &= ~63;
            bbox.xMax  = (bbox.xMax + 63) & ~63;
            bbox.yMax  = (bbox.yMax + 63) & ~63;

            gm.width   = (uint16_t)((bbox.xMax - bbox.xMin) >> 6);
            gm.height  = (uint16_t)((bbox.yMax - bbox.yMin) >> 6);
            gm.top     = -(int16_t)(bbox.yMax >> 6);
            gm.left    = (int16_t)(bbox.xMin >> 6);
            break;
        }

        case FT_GLYPH_FORMAT_BITMAP:
            if (this->pFontInst->fontInstFlags & fem::Embolden_Flag) {
                FT_GlyphSlot_Own_Bitmap(ftFace->glyph);
                FT_Bitmap_Embolden(gLibraryFT, &ftFace->glyph->bitmap, kBitmapEmboldenStrength, 0);
            }/* end if */

            gm.width   = (uint16_t)(ftFace->glyph->bitmap.width);
            gm.height  = (uint16_t)(ftFace->glyph->bitmap.rows);
            gm.top     = -(int16_t)(ftFace->glyph->bitmap_top);
            gm.left    = (int16_t)(ftFace->glyph->bitmap_left);
            break;

        default:
            assert(!"unknown glyph format");
            goto ERROR;
    }/* end switch */

    if (!this->pFontInst->subpixelPositioning) {
        gm.fAdvanceX = FEM26Dot6ToFEM16Dot16(ftFace->glyph->advance.x);
        gm.fAdvanceY = -FEM26Dot6ToFEM16Dot16(ftFace->glyph->advance.y);

        if (this->pFontInst->fontInstFlags & fem::DevKernText_Flag) {
            gm.rsbDelta = (int8_t)(ftFace->glyph->rsb_delta);
            gm.lsbDelta = (int8_t)(ftFace->glyph->lsb_delta);
        }/* end if */
    } else {
        gm.fAdvanceX = FT_MulFix(this->pFontInst->ftMatrix22.xx, ftFace->glyph->linearHoriAdvance);
        gm.fAdvanceY = -FT_MulFix(this->pFontInst->ftMatrix22.yx, ftFace->glyph->linearHoriAdvance);
    }/* end else if */

#ifdef ENABLE_GLYPH_SPEW
    FT_LOG("FT_Set_Char_Size(this:%p sx:%x sy:%x ", this, fracX, fracY);
    FT_LOG("Metrics(glyph:%d flags:%x) w:%d\n", glyphID, this->pFontInst->loadGlyphFlags, gm.width);
#endif

    FT_LOG("glyph : %d, width : %d, height : %d, top : %d, left : %d, advanceX : %d, advanceY : %d, rsbdelta : %d, lsbdelta : %d\n", glyphID, gm.width, gm.height, gm.top, gm.left, gm.fAdvanceX >> 16, gm.fAdvanceY >> 16, gm.rsbDelta, gm.lsbDelta);

    return gm;
}/* end method getGlyphMetrics */

GlyphOutline* FontScalerFT::getGlyphOutline(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY)
{
    android::Mutex::Autolock ac(gMutexFT);
    GlyphOutline* pGO = NULL;

    FT_UNUSED(fracX);
    FT_UNUSED(fracY);

    if (this->pFontInst->setupSize()) {
        return pGO;
    }/* end if */

    uint32_t flags = this->pFontInst->loadGlyphFlags;
    flags |= FT_LOAD_NO_BITMAP; // ignore embedded bitmaps so we're sure to get the outline
    flags &= ~FT_LOAD_RENDER;   // don't scan convert (we just want the outline)

    FT_Error err = FT_Load_Glyph(ftFace, glyphID, flags);
    if (err != 0) {
        FT_LOG("FT_Load_Glyph(glyph:%d flags:%d) returned %x\n",
                    glyphID, flags, err);
        return pGO;
    }/* end if */

    if (this->pFontInst->fontInstFlags & fem::Embolden_Flag) {
        emboldenOutline(&ftFace->glyph->outline);
    }/* end if */

    FT_GlyphSlot ftGS = ftFace->glyph;
    if (ftGS)
    {
        FEM26Dot6* x;
        FEM26Dot6* y;
        int16_t* contours;
        uint8_t* tags;

        FT_Outline& ftOtln = ftGS->outline;
        int nOtlnPts = ftOtln.n_points;
        int nContours = ftOtln.n_contours;
        FT_Vector* sOtlnPts = ftOtln.points;
        char* sOtlnTags = ftOtln.tags;
        int16_t* sCntrEndPts = ftOtln.contours;

        pGO = new GlyphOutline(nOtlnPts, nContours);
        if (NULL == pGO) {
            return pGO;
        }/* end if */

        x = pGO->x;
        y = pGO->y;
        tags = pGO->flags;
        contours = pGO->contours;

        for (int i = 0; i < nOtlnPts; ++i) {
            x[i] = sOtlnPts[i].x;
            y[i] = sOtlnPts[i].y;
            tags[i] = sOtlnTags[i] & 0x03;
        }/* end for */

        for (int i = 0; i < nContours; ++i) {
            contours[i] = sCntrEndPts[i];
        }/* end for */
    }/* end if */

    return pGO;
}/* end method getGlyphOutline */

void FontScalerFT::getFontMetrics(FontMetrics* mX, FontMetrics* mY)
{
    if (NULL == mX && NULL == mY) {
        return;
    }/* end if */

    android::Mutex::Autolock ac(gMutexFT);

    if (this->pFontInst->setupSize()) {
        ERROR:
        if (mX) {
            memset(mX, 0, sizeof(FontMetrics));
        }/* end if */
        if (mY) {
            memset(mY, 0, sizeof(FontMetrics));
        }/* end if */
        return;
    }/* end if */

    int upem = ftFace->units_per_EM;
    if (upem <= 0) {
        goto ERROR;
    }/* end if */

    FEM16Dot16 ptsX[6];
    FEM16Dot16 ptsY[6];
    FEM16Dot16 ys[6];
    FEM16Dot16 scaleY = this->pFontInst->fScaleY;
    FEM16Dot16 mxy = this->pFontInst->ftMatrix22.xy;
    FEM16Dot16 myy = this->pFontInst->ftMatrix22.yy;
    FEM16Dot16 xmin = (ftFace->bbox.xMin << 16) / upem;
    FEM16Dot16 xmax = (ftFace->bbox.xMax << 16) / upem;

    int leading = ftFace->height - (ftFace->ascender + -ftFace->descender);
    if (leading < 0) {
        leading = 0;
    }/* end if */

    /* Try to get the OS/2 table from the font. This contains the specific
     * average font width metrics which Windows uses. */
    TT_OS2* os2 = (TT_OS2*) FT_Get_Sfnt_Table(ftFace, ft_sfnt_os2);

    ys[0] = -ftFace->bbox.yMax;
    ys[1] = -ftFace->ascender;
    ys[2] = -ftFace->descender;
    ys[3] = -ftFace->bbox.yMin;
    ys[4] = leading;
    ys[5] = os2 ? os2->xAvgCharWidth : 0;

    FEM16Dot16 x_height;
    if (os2 && os2->sxHeight) {
        x_height = FT_MulDiv(this->pFontInst->fScaleX, os2->sxHeight, upem);
    } else {
        const FT_UInt x_glyph = FT_Get_Char_Index(ftFace, 'x');
        if (x_glyph) {
            FT_BBox bbox;
            FT_Load_Glyph(ftFace, x_glyph, this->pFontInst->loadGlyphFlags);
            if (this->pFontInst->fontInstFlags & fem::Embolden_Flag) {
                emboldenOutline(&ftFace->glyph->outline);
            }/* end if */
            FT_Outline_Get_CBox(&ftFace->glyph->outline, &bbox);
            x_height = (bbox.yMax << 16) / 64;
        } else {
            x_height = 0;
        }/* end else if */
    }/* end else if */

    /* convert upem-y values into scalar points */
    for (int i = 0; i < 6; i++) {
        FEM16Dot16 y = FT_MulDiv(scaleY, ys[i], upem);
        FEM16Dot16 x = FT_MulFix(mxy, y);
        y = FT_MulFix(myy, y);
        ptsX[i] = x;
        ptsY[i] = y;
    }/* end for */

    if (mX) {
        mX->fTop = ptsX[0];
        mX->fAscent = ptsX[1];
        mX->fDescent = ptsX[2];
        mX->fBottom = ptsX[3];
        mX->fLeading = ptsX[4];
        mX->fAvgCharWidth = ptsX[5];
        mX->fXMin = xmin;
        mX->fXMax = xmax;
        mX->fXHeight = x_height;

        FT_LOG("mX -- top : %d, ascent : %d, descent : %d, bottom : %d, leading : %d, avgCharWidth : %d, xmin : %d, xmax : %d, xheight : %d\n", (mX->fTop >> 16), (mX->fAscent >> 16), (mX->fDescent >> 16), (mX->fBottom >> 16), (mX->fLeading >> 16), (mX->fAvgCharWidth >> 16), (mX->fXMin >> 16), (mX->fXMax) >> 16, (mX->fXHeight >> 16));
    }/* end if */
    if (mY) {
        mY->fTop = ptsY[0];
        mY->fAscent = ptsY[1];
        mY->fDescent = ptsY[2];
        mY->fBottom = ptsY[3];
        mY->fLeading = ptsY[4];
        mY->fAvgCharWidth = ptsY[5];
        mY->fXMin = xmin;
        mY->fXMax = xmax;
        mY->fXHeight = x_height;

        FT_LOG("mY -- top : %d, ascent : %d, descent : %d, bottom : %d, leading : %d, avgCharWidth : %d, xmin : %d, xmax : %d, xheight : %d\n", (mY->fTop >> 16), (mY->fAscent >> 16), (mY->fDescent >> 16), (mY->fBottom >> 16), (mY->fLeading >> 16), (mY->fAvgCharWidth >> 16), (mY->fXMin >> 16), (mY->fXMax) >> 16, (mY->fXHeight >> 16));
    }/* end if */
}/* end method getFontMetrics */

static FT_Pixel_Mode compute_pixel_mode(fem::AliasMode format) {
    switch (format) {
        case fem::ALIAS_LCD_H:
        case fem::ALIAS_LCD_V:
            assert(!"An LCD format should never be passed here");
            return FT_PIXEL_MODE_GRAY;
        case fem::ALIAS_MONOCHROME:
            return FT_PIXEL_MODE_MONO;
        case fem::ALIAS_GRAYSCALE:
        default:
            return FT_PIXEL_MODE_GRAY;
    }
}/* end method compute_pixel_mode */

void FontScalerFT::getGlyphImage(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY, uint32_t rowBytes, uint16_t width, uint16_t height, uint8_t *buffer)
{
    android::Mutex::Autolock ac(gMutexFT);

    FT_Error    err;

    if (this->pFontInst->setupSize()) {
        goto ERROR;
    }/* end if */

    FT_LOG("glyph : %d width : %d height : %d rowBytes : %d\n", glyphID, width, height, rowBytes);

    err = FT_Load_Glyph(ftFace, glyphID, this->pFontInst->loadGlyphFlags);
    if (err != 0) {
        FT_LOG("FT_Load_Glyph(glyph:%d width:%d height:%d rb:%d flags:%d) returned %x\n",
                  glyphID, width, height, rowBytes, this->pFontInst->loadGlyphFlags, err);
    ERROR:
        memset(buffer, 0, rowBytes * height);
        return;
    }/* end if */

    const bool lcdRenderMode = this->pFontInst->maskFormat == fem::ALIAS_LCD_H ||
                               this->pFontInst->maskFormat == fem::ALIAS_LCD_V;

    switch ( ftFace->glyph->format ) {
        case FT_GLYPH_FORMAT_OUTLINE: {
            FT_Outline* outline = &ftFace->glyph->outline;
            FT_BBox     bbox;
            FT_Bitmap   target;
            int dx = 0, dy = 0;

            if (this->pFontInst->fontInstFlags & fem::Embolden_Flag) {
                emboldenOutline(outline);
            }/* end if */

            if (this->pFontInst->subpixelPositioning) {
                dx = fracX >> 10;
                dy = fracY >> 10;
                /* negate dy since freetype-y-goes-up and skia-y-goes-down */
                dy = -dy;
            }/* end if */

            FT_Outline_Get_CBox(outline, &bbox);

            /*
               what we really want to do for subpixel is
                   offset(dx, dy)
                   compute_bounds
                   offset(bbox & !63)
               but that is two calls to offset, so we do the following, which
               achieves the same thing with only one offset call.
            */
            FT_Outline_Translate(outline, dx - ((bbox.xMin + dx) & ~63),
                                          dy - ((bbox.yMin + dy) & ~63));

#if defined(SUPPORT_LCDTEXT)
            if (lcdRenderMode) {
                /* FT_Outline_Get_Bitmap cannot render LCD glyphs. In this case
                 * we have to call FT_Render_Glyph and memcpy the image out. */
                const bool isVertical = this->pFontInst->maskFormat == fem::ALIAS_LCD_V;
                FT_Render_Mode mode = isVertical ? FT_RENDER_MODE_LCD_V : FT_RENDER_MODE_LCD;

                /* TODO:
                 * FT_Render_Glyph(ftFace->glyph, mode);
                 *
                 * if (isVertical)
                 *     CopyFreetypeBitmapToVerticalLCDMask(glyph, ftFace->glyph->bitmap);
                 * else
                 *     CopyFreetypeBitmapToLCDMask(glyph, ftFace->glyph->bitmap);
                 */
                break;
            }/* end if */
#endif

            if (this->pFontInst->maskFormat == fem::ALIAS_LCD16) {
                FT_Render_Glyph(ftFace->glyph, FT_RENDER_MODE_LCD);
                copyFT2LCD16(rowBytes, width, height, (uint16_t*)buffer, ftFace->glyph->bitmap);
            } else {
                target.width = width;
                target.rows = height;
                target.pitch = rowBytes;
                target.buffer = buffer;
                target.pixel_mode = compute_pixel_mode(this->pFontInst->maskFormat);
                target.num_grays = 256;

                memset(buffer, 0, rowBytes * height);
                FT_Outline_Get_Bitmap(gLibraryFT, outline, &target);
            }/* end else if */
        } break;

        case FT_GLYPH_FORMAT_BITMAP: {
            if (this->pFontInst->fontInstFlags & fem::Embolden_Flag) {
                FT_GlyphSlot_Own_Bitmap(ftFace->glyph);
                FT_Bitmap_Embolden(gLibraryFT, &ftFace->glyph->bitmap, kBitmapEmboldenStrength, 0);
            }/* end if */

            FT_ASSERT_CONTINUE(width == ftFace->glyph->bitmap.width);
            FT_ASSERT_CONTINUE(height == ftFace->glyph->bitmap.rows);

            const uint8_t*  src = (const uint8_t*)ftFace->glyph->bitmap.buffer;
            uint8_t*        dst = buffer;

            if (ftFace->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY ||
                (ftFace->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO &&
                  this->pFontInst->maskFormat == fem::ALIAS_MONOCHROME)) {
                unsigned srcRowBytes = ftFace->glyph->bitmap.pitch;
                unsigned dstRowBytes = rowBytes;
                unsigned minRowBytes = srcRowBytes < dstRowBytes ? srcRowBytes : dstRowBytes;
                unsigned extraRowBytes = dstRowBytes - minRowBytes;

                for (int y = ftFace->glyph->bitmap.rows - 1; y >= 0; --y) {
                    memcpy(dst, src, minRowBytes);
                    memset(dst + minRowBytes, 0, extraRowBytes);
                    src += srcRowBytes;
                    dst += dstRowBytes;
                }/* end for */
            } else if (ftFace->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_MONO &&
                        (this->pFontInst->maskFormat == fem::ALIAS_GRAYSCALE ||
                        this->pFontInst->maskFormat == fem::ALIAS_LCD_H ||
                        this->pFontInst->maskFormat == fem::ALIAS_LCD_V)) {
                for (int y = 0; y < ftFace->glyph->bitmap.rows; ++y) {
                    uint8_t byte = 0;
                    int bits = 0;
                    const uint8_t* src_row = src;
                    uint8_t* dst_row = dst;

                    for (int x = 0; x < ftFace->glyph->bitmap.width; ++x) {
                        if (!bits) {
                            byte = *src_row++;
                            bits = 8;
                        }/* end if */

                        *dst_row++ = byte & 0x80 ? 0xff : 0;
                        bits--;
                        byte <<= 1;
                    }/* end for */

                    src += ftFace->glyph->bitmap.pitch;
                    dst += rowBytes;
                }/* end for */
            } else {
              assert(!"unknown glyph bitmap transform needed");
            }/* end else if */

            if (lcdRenderMode) {
                /* TODO: glyph.expandA8ToLCD(); */
            }/* end if */
        } break;

        default:
            assert(!"unknown glyph format");
            goto ERROR;
    }/* end switch */
}/* end method generateImage */

void FontScalerFT::emboldenOutline(FT_Outline* outline) {
    FT_Pos strength;
    strength = FT_MulFix(ftFace->units_per_EM, ftFace->size->metrics.y_scale) / 24;
    FT_Outline_Embolden(outline, strength);
}/* end method emboldenOutline */

//////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif
    FontEngine* getFontEngineInstance()
    {
        FT_LOG("\n");
        gFontEngineInstFT = new FontEngineFT();
        return (FontEngine*)gFontEngineInstFT;
    }/* getFontEngineInstance() */
#ifdef __cplusplus
}/* end extern "C" */
#endif

