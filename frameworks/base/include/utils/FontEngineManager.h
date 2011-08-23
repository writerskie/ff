/* frameworks/base/include/utils/FontEngineManager.h
**
** Copyright (c) 1989-2010, Bitstream Inc. and others. All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
** Redistributions of source code must retain the above copyright notice,
** this list of conditions and the following disclaimer.
** Redistributions in binary form must reproduce the above copyright notice,
** this list of conditions and the following disclaimer in the documentation
** and/or other materials provided with the distribution.
** Neither the name of Bitstream Inc. nor the names of its contributors may
** be used to endorse or promote products derived from this software without
** specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
** AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
** LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
** CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
** SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
** INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
** ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
** POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef __FONTENGINEMANAGER_HEADER__
#define __FONTENGINEMANAGER_HEADER__

#ifdef __cplusplus
extern "C" {
#endif
#include <stdint.h>
#include <stdlib.h>

    /** The font engine implementation will call this method to read font data
        from ROM/RAM/Disk. This method returns number of bytes read
        from input 'stream' in the given 'buffer' from the given 'offset'.
        @param stream    pointer to ROM/RAM/Disk stream representing the font
                         data.
        @param offset    the offset in bytes from the beginning of the font
                         data needed to be retrieve.
        @param buffer    pointer to the memory where the function needs to
                         write the font data.
        @param count     the number of bytes we need to retrieve starting at
                         the above offset.
        @return          On success returns nonzero value indicating the no of
                         bytes reads from the font data.
                         On faliure returns 0.
    */
    unsigned long streamRead(void*           stream,
                                 unsigned long   offset,
                                 unsigned char*  buffer,
                                 unsigned long   count );

    /** Method to close open Stream. To read stream, pls refer to 'StreamRead'
        method.
        @param stream    pointer to ROM/RAM/Disk stream representing the font
                         data.
    */
    void streamClose(void*  stream);
#ifdef __cplusplus
}/* end extern "C" */
#endif

class FontEngine;

/* 32 bit signed integer used to represent fractions values with 16 bits to the right of the decimal point */
typedef int32_t FEM16Dot16;
#define FEMOne16Dot16          (1 << 16)

/* 32 bit signed integer used to represent fractions values with 6 bits to the right of the decimal point */
typedef int32_t FEM26Dot6;
#define FEMOne26Dot6           (1 << 6)

typedef FontEngine* (*getFontEngineInstanceType)();
typedef void (*releaseFontEngineInstanceType)(FontEngine*);

namespace fem
{
    /** FontStyle specifies the intrinsic style attributes of a given typeface */
    typedef enum
    {
        STYLE_NORMAL     = 0,    /* Normal/Roman/Regular/Plain Typeface Style */
        STYLE_BOLD       = 0x1,  /* Bold Typeface Style */
        STYLE_ITALIC     = 0x2,  /* Italic Typeface Style */
        STYLE_BOLDITALIC = 0x3   /* Bold and Italic Typeface Style */
    }FontStyle;

    /** Specifies the alaising mode. */
    typedef enum
    {
        ALIAS_MONOCHROME = 0,  /* 1 bit per pixel */
        ALIAS_GRAYSCALE  = 1,  /* 8 bit per pixel */

        /** The LCD formats look like this in memory:

            First, there's an A8 plane which contains the average alpha value for
            each pixel. Because of this, the LCD formats can be passed directly
            to functions which expect an A8 and everything will just work.

            After that in memory, there's a bitmap of 32-bit values which have
            been RGB order corrected for the current screen (based on the
            settings in SkFontHost at the time of renderering). The alpha value
            for each pixel is the maximum of the three alpha values.

            kHorizontalLCD_Format has an extra column of pixels on the left and right
            edges. kVerticalLCD_Format has an extra row at the top and bottom.
        */
        ALIAS_LCD_H      = 2,  /* 4 bytes per pixel : a/r/g/b */
        ALIAS_LCD_V      = 3,  /* 4 bytes per pixel : a/r/g/b */

        ALIAS_LCD16      = 4   /* 565 alpha for r/g/b */
    }AliasMode;

    /** Specifies the type of hinting to do on font outlines. This controls
        whether to fit font outlines to the pixel grid, and if so, whether to
        optimize for fidelity (faithfulness to the original outline shapes) or
        contrast.
    */
    typedef enum
    {
        HINTING_NONE   = 0,
        HINTING_LIGHT  = 1,
        HINTING_NORMAL = 2,
        HINTING_FULL   = 3
    }Hinting;

    /** Specifies the font engine capablity i.e type of alias mode the engine
        supports.
    */
    typedef enum
    {
        CAN_RENDER_MONO   = 0,
        CAN_RENDER_GRAY   = 0x1,
        CAN_RENDER_LCD_H  = 0x2,
        CAN_RENDER_LCD_V  = 0x4,
        CAN_RENDER_LCD    = 0x6
    } EngineCapability;

    /** Specifies the bit masks to query the status of FontScalerInfo's flag
        bits by Font Engine.

        DevKernText flag indicates the support for lsbDelta and rsbDelta
        by the font scaler. If flag set to fontscaler then these values will be
        return as a part GlyphMetrics by a call to getGlyphMetrics().
        http://www.mail-archive.com/freetype-devel@nongnu.org/msg01846.html
        These values record the displacement of left and right edge in each
        due to auto-hinting, and this can be useful to correct the space
        between two glyph images to better respect their original spacing.

        In some fonts, there are several sizes of embedded bitmap fonts.
        Embedding bitmap fonts can improve the on-screen appearance of small
        pixel sizes. A bitmap, if found for the corresponding glyph and pixel
        size by the font engine/renderer, will be return provided embedded
        bitmap flag is set for the font scaler.

        Font hinting is the use of mathematical instructions to adjust the
        display of an outline font so that it lines up with a rasterized grid.
        At low screen resolutions, hinting is critical for producing a clear,
        legible text for human readers. Hinting flag indicates hinting
        (see Hinting enum) is applied to the font scaler.

        Emboldening flag indicates that algorithmic emboldening is applied to
        the font scaler. A FontEngine will increases weight by applying a
        widening algorithm to the glyph outline This may be used to simulate
        a bold weight where no designed bold weight is available.
    */
    enum Flags
    {
        DevKernText_Flag        = 0x01, /* mask for querying the status of a kerning bit  */
        Hinting_Flag            = 0x06, /* mask for querying the status of a hinting bits */
        EmbeddedBitmapText_Flag = 0x08, /* mask for querying the status of an embedded bitmap bit */
        Embolden_Flag           = 0x10  /* mask for querying the status of an emboldening bit */
    };

    /** Specifies the different types of font format */
    typedef enum
    {
        TYPE1_FONT = 0,
        TYPE1CID_FONT = 1,
        CFF_FONT = 2,
        TRUETYPE_FONT = 3,
        OTHER_FONT = 4,
        NOTEMBEDDABLE_FONT = 5
    }FontType;


    /** These enum values match the values used in the PDF file format. */
    typedef enum
    {
        FIXEDPITCH_STYLE  = 0x00001,
        SERIF_STYLE       = 0x00002,
        SYMBOLIC_STYLE    = 0x00004,
        SCRIPT_STYLE      = 0x00008,
        NONSYMBOLIC_STYLE = 0x00020,
        ITALIC_STYLE      = 0x00040,
        ALLCAPS_STYLE     = 0x10000,
        SMALLCAPS_STYLE   = 0x20000,
        FORCEBOLD_STYLE   = 0x40000
    }StyleFlags;
};

/** \struct FontMetrics

    This struct provides font metrics information.

    These are scaled values expressed in fractional pixels represented in the
    16.16 fixed point floating format.
*/
struct FontMetrics
{
    FEM16Dot16  fTop;           /* The greatest distance above the baseline
                                   for any glyph using the "positive Y downwards"
                                   convention. */
    FEM16Dot16  fAscent;        /* The recommended distance above the baseline
                                   using the "positive Y downwards"convention.
                                   This is recommended as this is not always
                                   exactly equal to the maximum of the extents
                                   of all the glyphs in the font, but rather is
                                   picked to express the font designer's intent
                                   as to how the font should align with elements
                                   above it. */
    FEM16Dot16  fDescent;       /* The recommended distance below the baseline
                                   using the "positive Y downwards"convention.
                                   This is recommended as this is not always
                                   exactly equal to the maximum of the extents
                                   of all the glyphs in the font, but rather is
                                   picked to express the font designer's intent
                                   as to how the font should align with elements
                                   below it. */
    FEM16Dot16  fBottom;        /* The greatest distance below the baseline for
                                   any glyph using the "positive Y downwards"
                                   convention. */
    FEM16Dot16  fLeading;       /* The recommended distance to add between lines
                                   of text using the "positive Y downwards"
                                   convention. This is used to compute the
                                   'height' (recommended vertical distance
                                   between baselines when setting consecutive
                                   lines of text with the font).
                                   height = fBottom - fTop + fLeading */
    FEM16Dot16  fAvgCharWidth;  /* The average character width (>= 0) */
    FEM16Dot16  fXMin;          /* The minimum bounding box x value for all glyphs */
    FEM16Dot16  fXMax;          /* The maximum bounding box x value for all glyphs */
    FEM16Dot16  fXHeight;       /* The height of an 'x' in px, or 0 if no 'x' in face */
};/* end class FontMetrics */

/** \class GlyphMetrics

    This class represents information for a single glyph. A glyph is the visual
    representation of one or more characters. Many different glyphs can be
    used to represent a single character or combination of characters.

    Metrics available through GlyphMetrics are the components of the advance,
    the visual bounds, and the left and right side bearings.

    Glyphs for a rotated font, or obtained from after applying a rotation to
    the glyph, can have advances that contain both X and Y components. Usually
    the advance only has one component.

    The advance of a glyph is the distance from the glyph's origin to the
    origin of the next glyph along the baseline, which is either vertical
    or horizontal.

    lsbDelta and rsbDelta record the displacement of left and right edge in
    glyph due to auto-hinting, and this can be useful to correct the space
    two glyph images to better respect their original spacing.

    Here a small pseudo code fragment which shows how to use 'lsb_delta' and 'rsb_delta':
    http://www.freetype.org/freetype2/docs/reference/ft2-base_interface.html#FT_GlyphSlotRec

      FT_Pos  origin_x       = 0;
      FT_Pos  prev_rsb_delta = 0;


      for all glyphs do
        <compute kern between current and previous glyph and add it to
         `origin_x'>

        <load glyph with 'FT_Load_Glyph'>

        if ( prev_rsb_delta - face->glyph->lsb_delta >= 32 )
          origin_x -= 64;
        else if ( prev_rsb_delta - face->glyph->lsb_delta < -32 )
          origin_x += 64;

        prev_rsb_delta = face->glyph->rsb_delta;

        <save glyph image, or render glyph, or ...>

        origin_x += face->glyph->advance.x;
      endfor

    The values contained here are all in fractional pixels, and not in
    original font units. Also glyph may be hinted (if hinting is on ), so do
    not assume that these are the linearly-scaled values.
*/
class GlyphMetrics
{
public:
    GlyphMetrics()
        : lsbDelta(0), rsbDelta(0),
           width(0), height(0),
           fAdvanceX(0), fAdvanceY(0),
           left(0), top(0)
    {}

    ~GlyphMetrics() {}

    /** Call this to set all of the metrics fields to 0 (e.g. if the scaler
        encounters an error measuring a glyph). Note: this does not alter the
        fImage, fPath, fID, fMaskFormat fields.
     */
    void clear()
    {
        rsbDelta = 0; lsbDelta = 0;
        width = 0; height = 0;
        fAdvanceX = 0; fAdvanceY = 0;
        top = 0; left = 0;
    }

    int8_t      lsbDelta;   /* The difference between hinted and unhinted
                               left side bearing while autohinting is active.
                               Zero otherwise. Value is expressed in fractional
                               pixels (where 1 unit = 1/64th pixel). */

    int8_t      rsbDelta;   /* The difference between hinted and unhinted
                               right side bearing while autohinting is active.
                               Zero otherwise. Value is expressed in fractional
                               pixels (where 1 unit = 1/64th pixel). */

    uint16_t    width;      /* The width of the glyph's bounding box in
                               pixels. */

    uint16_t    height;     /* The height of the glyph's bounding box in
                               pixels. */

    FEM16Dot16  fAdvanceX;  /* The horizontal distance (in fractional pixels
                               represented in the 16.16 fixed point floating
                               format) from the glyph's origin to the origin
                               of the next glyph along the baseline. */

    FEM16Dot16  fAdvanceY;  /* The vertical distance (in fractional pixels
                               represented in the 16.16 fixed point floating
                               format) from the glyph's origin to the origin
                               of the next glyph along the baseline. */

    int16_t     left;       /* The glyph's bounding box left edge coordinate
                               in pixels. */

    int16_t     top;        /* The glyph's bounding box's top edge coordinate,
                               using the "positive Y downwards" convention. */
};/* end class GlyphMetrics */


/** \struct FontScalerInfo

    This struct provides font scaler information. This information is passed
    to the font engine plugin to create a font scaler.
*/
struct FontScalerInfo
{
    /*
       The ID should be unique for the underlying font file/data, not unique
       per FontScaler instance. Thus it is possible/common to request a
       FontScaler for the same font more than once (e.g. asking for the same
       font by name several times).The FontEngine may return seperate
       FontScaler instances in that case, or it may choose to use a cache and
       return the same instance.

       The caller (eg: graphic/text layout engine etc) is responsible for
       assigning this ID. However a rendering engine may choose not to provide
       the same; in that case font engine need to restort to some other
       mechanism to identify the font (like font file name).
    */
    uint32_t                        fontID;

    /*
       With subpixel positioning technology, a font engine can accurately
       adjust and control the placement of characters.

       Without it, outlines  of character shapes are simply fitted to
       character grids in an output device (such as a laser printer or a
       computer screen). If a pixel fell within the outline of a character
       shape, the pixel was turned on. If not, the pixel was left off.

       Subpixel positioning allows glyphs to start within the pixel and not
       just the beginning boundary of the pixel. Because of this extra
       resolution in positioning glyphs, the spacing and proportions of the
       glyphs is more precise and consistent.
    */
    bool                             subpixelPositioning;

    fem::AliasMode                   maskFormat;       /* specify the aliasing mode. */

    /*
       Specify the kerning, hinting, emboldening and embedded-bitmap status
       for the font scaler.
    */
    uint8_t                         flags;

    /* scaling factors along the x-axis. Rendering is resized according to the
       specified scaling factor value. Value is in fractional pixels
       represented in the 16.16 fixed point floating format.
    */
    FEM16Dot16                      fScaleX;

    /* scaling factors along the y-axis. Rendering is resized according to the
       specified scaling factor value. Value is in fractional pixels
       represented in the 16.16 fixed point floating format.
    */
    FEM16Dot16                      fScaleY;

    /*
       The amount to skew along the x-axis. Values is in fractional pixels
       represented in the 16.16 fixed point floating format.

       The skewX transformation has an effect of pushing all the x-coordinates
       by the angle. Visually, this makes vertical lines appear at an angle.
    */
    FEM16Dot16                      fSkewX;

    /*
       The amount to skew along the y-axis. Values is in fractional pixels
       represented in the 16.16 fixed point floating format.

       The skewY transformation has an effect of pushing all the y-coordinates
       by the angle. Visually, this makes horizontal lines appear at an angle.
    */
    FEM16Dot16                      fSkewY;

    void*                           pStream;           /* font input stream. Can be null; in case pBuffer or pPath is used to create a font instance. */

    const char*                     pPath;             /* system path to font file. Can be null; in case fontID is non zero and pBuffer or pPath is used to create a font instance. */
    const uint8_t*                  pBuffer;           /* font file buffer. Can be null; in case pStream or pPath is used to create a font instance. */
    size_t                          pathSz;            /* font file path length. */
    size_t                          size;              /* buffer size if (pBuffer != NULL); input stream size otherwise. */
};/* end struct FontScalerInfo */

/** \class GlyphOutline

    This class provides outline information for a glyph.

    Outlines is a collection of closed paths called contours. Each contour
    delimits an outer or inner region of the glyph, and can be made of either
    line segments or Bezier arcs.

    The arcs are defined through control points, and can be either second-order
    (these are conic Beziers) or third-order (cubic Beziers) polynomials,
    depending on the font format. Note that conic Beziers are usually called
    quadratic Beziers in the literature. Hence, each point of the outline has
    an associated flag indicating its type (normal or control point).

    These rules are applied to decompose the contour:
    1. Two successive `on' points indicate a line segment joining them.
    2. One `off' point amidst two `on' points indicates a conic bezier, the
    `off' point being the control point, and the `on' ones the start and end
    points.
    3. Finally, two successive `off' points amidst two `on' points indicates
    a cubic bezier, the `off' points being the control points, and the `on'
    ones the start and end points.

                                      *              # on
                                                     * off
                                   __---__
      #-__                      _--       -_
          --__                _-            -
              --__           #               \
                  --__                        #
                      -#
                               Two `on' points
       Two `on' points       and one `off' point
                                between them

                    *
      #            __      Two `on' points with two `off'
       \          -  -     points between them.  The point
        \        /    \    marked `0' is the middle of the
         -      0      \   `off' points, and is a `virtual
          -_  _-       #   on' point where the curve passes.
            --             It does not appear in the point
                           list.
            *

    In creating the glyph outlines, a type designer uses an imaginary square
    called the EM square. Typically, the EM square can be thought of as a
    tablet on which the characters are drawn. The square's size, i.e., the
    number of grid units on its sides. It is the reference used to scale the
    outlines to a given text dimension. The greater the EM size is, the
    larger resolution the designer can use when digitizing outlines.

    ox, oy represents the actual points in 'design' coordinates. These are the
    unscaled unhinted points.

    x, y represents the actual points in 'device' coordinates expressed in
    32-bit fractional pixel values, using  the 26.6  fixed float  format
    (which uses  26 bits for the integer part, and 6 bits for the fractional
    part).
*/
class GlyphOutline
{
private:
    GlyphOutline();

public:
    /** Constuctor to create a GlyphOutline object.
        @param nOtlnPts    number of outline's points in the glyph.
        @param nContours   number of contours in glyph.
    */
    GlyphOutline(int16_t nOtlnPts, int16_t nContours);
    ~GlyphOutline();

    int16_t     contourCount;  /* number of contours in glyph */
    int16_t     pointCount;    /* number of points in the glyph */

    /* pointers to an array of `pointCount' elements, giving the outline's
       point coordinates. These points are expressed in 32-bit fractional
       pixel values, using the  26.6 fixed float format.
    */
    FEM26Dot6  *x, *y;

    /*
       A pointer to an array of `pointCount' elements, giving each contour
       end point.
    */
    int16_t*    contours;

    /*
       A pointer to an array of `pointCount' chars, giving information about
       each contour point's.

       If bit 0 is unset, the point is `off' the curve, i.e. a Bezier control
       point, while it is `on' when set.

       Bit 1 is meaningful for `off' points only. If set, it indicates a
       third-order Bezier arc control point; and a second-order control point
       if unset.
    */
    uint8_t*    flags;         /* the points flags */
}; /* end class GlyphOutline */

/** \class AdvancedTypefaceMetrics

    The AdvancedTypefaceMetrics will be used to used by the PDF backend to
    correctly embed typefaces.  This class is filled in with information about
    a given typeface by FontScaler.
*/
class AdvancedTypefaceMetrics {
public:
    char*      pFontName;

    // The type of the underlying font program.  This field determines which
    // of the following fields are valid.  If it is kOther_Font or
    // kNotEmbeddable_Font, the per glyph information will never be populated.
    fem::FontType   fType;

    // The bounding box of all glyphs (in font units).
    FEM26Dot6  fXMin, fYMin;
    FEM26Dot6  fXMax, fYMax;

    int32_t    fNumGlyphs;
    int32_t    fNumCharmaps;

    uint16_t   fStyle; // Font style characteristics.
    uint16_t   fEmSize; // The size of the em box (defines font units).
    int16_t    fItalicAngle; // Counterclockwise degrees from vertical of the
                          // dominant vertical stroke for an Italic face.

    // The following fields are all in font units.
    int16_t    fAscent; // Max height above baseline, not including accents.
    int16_t    fDescent; // Max depth below baseline (negative).
    int16_t    fStemV; // Thickness of dominant vertical stem.
    int16_t    fCapHeight; // Height (from baseline) of top of flat capitals.
    int16_t    fMaxAdvWidth; // The maximal advance width, for all glyphs in this face. .

    bool       isMultiMaster; // isMultiMaster may be true for Type1_Font or CFF_Font.
    bool       isScalable;
    bool       hasVerticalMetrics;
};

/** \class FontScaler

    Font Scaler Interface; each plugin will provide its own implementation.
*/
class FontScaler
{
public:
    FontScaler() {}
    virtual ~FontScaler() {}

    /** Returns the number of glyphs in the font.
    */
    virtual uint16_t getGlyphCount() const = 0;

    /** Returns the glyph index of the character code (unicode value) in the
        font. The glyph index is a number from 0 to n-1, assuming the font
        contains 'n' number of glyphs. Glyph Index will be zero if unicode is
        not present in the font.
        @param charUniCode    character code (unicode).
        @return the glyph index for the given character code.
    */
    virtual uint16_t getCharToGlyphID(int32_t charUniCode) = 0;

    /** Returns the character code (unicode value) of the glyph index in the
        font. character code will be zero if glyph index doesn't correspond to
        individual Unicode codepoint.
        @param glyphID    glyph index.
        @return the character code (unicode value) of the given glyph index.
    */
    virtual int32_t getGlyphIDToChar(uint16_t glyphID) = 0;

    /** Returns the GlyphMetrics object for the given glyph index.
        @param glyphID    glyph index.
        @param fracX      horizontal factional pen delta; normally set to
                          zero. Use it with non-zero values if you are also
                          using fractional character positioning.
        @param fracY      vertical factional pen delta; normally set to
                          zero. Use it with non-zero values if you are also
                          using fractional character positioning.
        @return the GlyphMetrics object for the given glyph. GlyphMetric
        instance values will be set to 0 in case of a bad glyphID or function
        failure; set fAdvanceX and fAdvanceY to valid values and other fields
        to 0 otherwise.
    */
    virtual GlyphMetrics getGlyphAdvance(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY) = 0;

    /** Returns the GlyphMetrics object for the given glyph index.
        @param glyphID    glyph index.
        @param fracX      horizontal factional pen delta expressed in
                          fractional pixels in the 16.16 fixed point floating
                          format; normally set to zero. Use it with non-zero
                          values if you are also using fractional character
                          positioning.
        @param fracY      vertical factional pen delta expressed in
                          fractional pixels in the 16.16 fixed point floating
                          format; normally set to zero. Use it with non-zero
                          values if you are also using fractional character
                          positioning.
        @return the GlyphMetrics object for the given glyph. GlyphMetric
        instance values will be set to 0 in case of a bad glyphID or function
        failure; set glyph metrics (width, height, top, left, lsbDelta,
        rsbDelta) to valid values otherwise.
    */
    virtual GlyphMetrics getGlyphMetrics(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY) = 0;

    /** Render the specified glyph into a 'buffer' (user allocated) of given
        rowBytes and height. On success, the API returned the "positive pitch"
        buffer; cleared to 0 otherwise. The rows are stored in decreasing
        vertical position; the first bytes of the pixel buffer are part of the
        upper bitmap row. Glyph image origin is always mapped to lower-left
        corner of the buffer.
        @param glyphID    glyph index.
        @param fracX      horizontal factional pen delta expressed in
                          fractional pixels in the 16.16 fixed point floating
                          format; normally set to zero. Use it with non-zero
                          values if you are also using fractional character
                          positioning.
        @param fracY      vertical factional pen delta expressed in
                          fractional pixels in the 16.16 fixed point floating
                          format; normally set to zero. Use it with non-zero
                          values if you are also using fractional character
                          positioning.
        @param rowBytes   buffer's row bytes.
        @param width      buffer's width.
        @param height     buffer height.
        @param buffer     user allocated buffer.
    */
    virtual void getGlyphImage(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY, uint32_t rowBytes, uint16_t width, uint16_t height, uint8_t *buffer) = 0;

    /** Returns the font wide metrics. The API simply returns in case both
        mX, mY are NULL; set them to vaild values in case operation succeed
        or 0 in case of error.
        @param mX    If not null, returns the horizontal font wide metric.
        @param mY    If not null, returns the vertical font wide metric.
    */
    virtual void getFontMetrics(FontMetrics* mX, FontMetrics* mY) = 0;

    /** Returns the outline for the glyph, given the glyph id. User should
        delete the GlyphOutline object when done.

        GlyphOutline scaled coordinates are in device coordinate expressed
        in fractional pixels (26.6 fixed point floating format).
        @param glyphID    glyph index.
        @param fracX      horizontal factional pen delta expressed in
                          fractional pixels in the 16.16 fixed point floating
                          format; normally set to zero. Use it with non-zero
                          values if you are also using fractional character
                          positioning.
        @param fracY      vertical factional pen delta expressed in
                          fractional pixels in the 16.16 fixed point floating
                          format; normally set to zero. Use it with non-zero
                          values if you are also using fractional character
                          positioning.
        @return the outline for the given glyph.
    */
    virtual GlyphOutline* getGlyphOutline(uint16_t glyphID, FEM16Dot16 fracX, FEM16Dot16 fracY) = 0;
};/* end class FontScaler */

/** \class FontEngine

    Font Engine Interface; each plugin will provide its implementation.
*/
class FontEngine
{
public:
    /** Default constructor
    */
    FontEngine() {}

    /** Destructor
    */
    virtual ~FontEngine() {}

    /** Returns font engine name.
    */
    virtual const char* getName() const = 0;

    /** Returns font engine's capabilities.
        @param desc    The information about the font scaler.
    */
    virtual fem::EngineCapability getCapabilities(FontScalerInfo& desc) const = 0;

    /** Creates and returns font scaler on sucess; null otherwise.
        @param desc    The information about the font scaler.
    */
    virtual FontScaler* createFontScalerContext(const FontScalerInfo& desc) = 0;

    /** For the given font; returns the font name, name's length,
        style. It also return a flag which tells about whether the font
        fixed width.
        @param path            The system path to font file.
        @param name            If not null, returns name of the font
                               specifed by font path.
        @param length          The maximum space allocated in path (by the
                               caller). In case of overflow;
                               strlen(fontname) > length; returned name is
                               'length' byte long. Also the returned name is
                               not zero-terminated.
        @param style           If not null, return style of the specified font.
        @param isFixedWidth    If not null, return whether font is fixed-pitch
                               or variable-width.
        @return on success, length of font name; zero otherwise.
    */
    virtual size_t getFontNameAndAttribute(const char path[], char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth) = 0;

    /** For the given font; returns the font name, name's length and
        style. It also return a flag which tells about whether the font
        fixed width.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @param name            If not null, returns name of the font specifed
                               by font buffer.
                               The returned name is 'length' byte long.
        @param length          The maximum space allocated in path (by the
                               caller). In case of overflow;
                               strlen(fontname) > length; returned name is 'length'
                               byte long. Also the returned name is not zero-terminated.
        @param style           If not null, return style of the specified font.
        @param isFixedWidth    If not null, return whether font is fixed-pitch
                               or variable-width.
        @return on success, length of font name; zero otherwise.
    */
    virtual size_t getFontNameAndAttribute(const void* buffer, const uint32_t bufferLength, char name[], size_t length, fem::FontStyle*  style, bool* isFixedWidth) = 0;

    /** For the given font, returns 'true' if the font format is supported;
        'false' otherwise.
        @param path      The system path to font file.
        @param isLoad    If 'true' font file will be loaded i.e. font object
                         (sfnt etc.) will be created to determine the font
                         file support.
                         If 'false' file extension will be checked against
                         the predefined list of engine supported font formats
                         to determine the font file support.
    */
    virtual bool isFontSupported(const char path[], bool isLoad) = 0;

    /** For the given font, returns 'true' if the font format is supported;
        'false' otherwise.  In case of supported Fonts, font object
        (sfnt etc.) will be created.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
    */
    virtual bool isFontSupported(const void* buffer, const uint32_t bufferLength) = 0;

    /** Given system path of the font file; returns the number of font units
        per em.
        @param path    The system path to font file.
        @return the number of font units per em or 0 on error.
    */
    virtual uint32_t getFontUnitsPerEm(const char path[]) = 0;

    /** Given font data in buffer; returns the number of font units per em.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @return the number of font units per em or 0 on error.
    */
    virtual uint32_t getFontUnitsPerEm(const void* buffer, const uint32_t bufferLength) = 0;

    /** Returned whether the font is embeddable or not.
        @param path    The system path to font file.
        @return true in case font is embeddable; false otherwise.
    */
    virtual bool canEmbed(const char path[]) = 0;

    /** The API tells whether the font is embeddable or not.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @return true in case font is embeddable; false otherwise.
    */
    virtual bool canEmbed(const void* buffer, const uint32_t bufferLength) = 0;

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
    virtual uint32_t getGlyphsAdvance(const char path[], uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance) = 0;

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
    virtual uint32_t getGlyphsAdvance(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance) = 0;

    /** Given system path of the font file; returns the glyph names.
        @param path           The system path to font file.
        @param start          The first glyph index.
        @param count          The number of glyph name values you want to
                              retrieve.
        @param pGlyphsName    The glyph names. This array must contain at
                              least 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    virtual uint32_t getGlyphsName(const char path[], uint32_t start, uint32_t count, char** pGlyphsName) = 0;

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
    virtual uint32_t getGlyphsName(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, char** pGlyphsName) = 0;


    /** Given system path of the font file; returns the glyph unicodes.
        @param path              The system path to font file.
        @param start             The first glyph index.
        @param count             The number of unicode values you want to retrieve.
        @param pGlyphsUnicode    The glyph unicodes. This array must contain at least
                                 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    virtual uint32_t getGlyphsUnicode(const char path[], uint32_t start, uint32_t count, int32_t* pGlyphsUnicode) = 0;

    /** Given font data in buffer; returns the glyph unicodes.
        @param buffer            The font file buffer.
        @param bufferLength      Length of the buffer.
        @param start             The first glyph index.
        @param count             The number of unicode values you want to
                                 retrieve.
        @param pGlyphsUnicode    The glyph unicodes. This array must contain
                                 at least 'count' elements.
        @return 0 on success; return non-zero in case of error.
    */
    virtual uint32_t getGlyphsUnicode(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, int32_t* pGlyphsUnicode) = 0;

    /** Retrieve detailed typeface metrics.  Used by the PDF backend.
        @param path    The system path to font file.
        @return A pointer to vaild object on success; NULL is returned if
        the font is not found.
    */
    virtual AdvancedTypefaceMetrics* getAdvancedTypefaceMetrics(const char path[]) = 0;

    /** Retrieve detailed typeface metrics.  Used by the PDF backend.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @return A pointer to vaild object on success; NULL is returned if
        the font is not found.
    */
    virtual AdvancedTypefaceMetrics* getAdvancedTypefaceMetrics(const void* buffer, const uint32_t bufferLength) = 0;
};

/** \struct FontEngineInfo

    This struct provides information about a font engine implementation.
*/
struct FontEngineInfo
{
    const char*  name;
}; /* end struct FontEngineInfo */

typedef FontEngineInfo*         FontEngineInfoPtr;
typedef FontEngineInfo**        FontEngineInfoArrPtr;
typedef FontEngineInfo** const  FontEngineInfoArrCPtr;

class FontEngineManager
{
public:
    /** Returns a singleton instance to a font engine manager.
    */
    static FontEngineManager& getInstance();

    /** Returns font scaler instance.
        @param desc    The information about the font scaler.
    */
    FontScaler* createFontScalerContext(const FontScalerInfo& desc);

    /** Returns the count of available font engines.
    */
    size_t getFontEngineCount() const { return engineCount; }

    /** Given a font engine name; returns its instance.
    */
    FontEngine* getFontEngine(const char name[]);

    /** Returns a list all available font engines. Font engine manager is
        resposible to free it.
    */
    FontEngineInfoArrCPtr listFontEngines() { return (FontEngineInfoArrCPtr)pFontEngineInfoArr; }

    /** Given system path of the font file; returns the fone name, name's
        length and style. It also return a flag which tells about whether the
        font fixed width.
        @param path            The system path to font file.
        @param name            If not null, returns name of the specifed font
                               path. The returned name is 'length' byte long.
        @param length          The maximum space allocated in path (by the caller).
        @param style           If not null, return style of the specified font.
        @param isFixedWidth    If not null, return whether font is fixed-pitch
                               or variable-width.
        @return on success, length of font name; zero otherwise.
    */
    size_t getFontNameAndAttribute(const char path[], char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth);

    /** Given font data in buffer; returns its name, name's length and
        style. It also return a flag which tells about whether the font
        fixed width.
        @param buffer          The font file buffer.
        @param bufferLength    Length of the buffer.
        @param name            If not null, returns name of the specifed font
                               path. The returned name is 'length' byte long.
        @param length          The maximum space allocated in path (by the caller).
        @param style           If not null, return style of the specified font.
        @param isFixedWidth    If not null, return whether font is fixed-pitch
                               or variable-width.
        @return on success, length of font name; zero otherwise.
    */
    size_t getFontNameAndAttribute(const void* buffer, const uint32_t bufferLength, char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth);

    /** Given system path of the font file; returns 'true' if the font format is
        supported; 'false' otherwise.
        @param path      The system path to font file.
        @param isLoad    If 'true' font file will be loaded i.e. font object
                         (sfnt etc.) will be created to determine the font
                         file support.
                         If 'false' file extension will be checked against
                         the predefined list of engine supported font formats
                         to determine the font file support.
    */
    bool isFontSupported(const char path[], bool isLoad);

    /** Given font data in buffer; returns 'true' if the font format is
        supported; 'false' otherwise.
        @param buffer    The font file buffer.
        @param isLoad    If 'true' font file will be loaded i.e. font object
                         (sfnt etc.) will be created to determine the font
                         file support.
                         If 'false' file extension will be checked against
                         the predefined list of engine supported font formats
                         to determine the font file support.
    */
    bool isFontSupported(const void* buffer, const uint32_t bufferLength);

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
    typedef struct FontEngineNode_t
    {
        struct FontEngineNode_t*  next;
        FontEngine*               inst;
    } FontEngineNode;

    size_t                     engineCount;         /* No. of available font engines */
    FontEngineNode*            pFontEngineList;     /* All available font engines */

    FontEngineInfoArrPtr       pFontEngineInfoArr;  /* All available font engines info */

    static FontEngineManager*  pFEMInst;            /* Pointer to singleton font engine manager's instance */

    FontEngineManager();
    ~FontEngineManager();

    FontEngineManager(const FontEngineManager &);
    FontEngineManager& operator=(const FontEngineManager &);
};/* end class FontEngineManager */

#endif /* __FONTENGINEMANAGER_HEADER__ */

