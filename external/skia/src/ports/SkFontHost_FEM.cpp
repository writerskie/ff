/* src/ports/SkFontHost_FEM.cpp
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

#include "SkScalerContext.h"
#include "SkDescriptor.h"
#include "SkFontHost.h"
#include "SkMask.h"
#include "SkStream.h"
#include "SkString.h"
#include "SkThread.h"
#include "SkMatrix.h"

#include <utils/FontEngineManager.h>

/* #define SK_ENABLE_LOG */

#ifdef SK_ENABLE_LOG
/* fprintf, FILE */
#include <stdio.h>

static FILE * fplog = NULL;

#define SK_STARTLOG fplog = fopen("/data/sklog.txt", "a");
#define SK_LOG(...) \
        SK_STARTLOG \
        fprintf(fplog, __FUNCTION__); \
        fprintf(fplog, ", "); \
        fprintf(fplog, __VA_ARGS__); \
        SK_ENDLOG
#define SK_ENDLOG fclose(fplog);
#else
#define SK_STARTLOG
#define SK_LOG(...)
#define SK_ENDLOG
#endif /* SK_ENABLE_LOG */

#ifdef __cplusplus
extern "C" {
#endif
    unsigned long streamRead(void*           stream,
                                 unsigned long   offset,
                                 unsigned char*  buffer,
                                 unsigned long   count )
    {
        SkStream* str = (SkStream*)stream;

        if (count) {
            if (!str->rewind()) {
                return 0;
            } else {
                unsigned long ret;
                if (offset) {
                    ret = str->read(NULL, offset);
                    if (ret != offset) {
                        return 0;
                    }/* end if */
                }/* end if */

                ret = str->read(buffer, count);

                if (ret != count) {
                    return 0;
                }/* end if */
                count = ret;
            }/* end else if */
        }/* end if */
        return count;
    }/* end method streamRead */

    void streamClose(void*  stream) {}/* end method streamClose */
#ifdef __cplusplus
}/* end extern "C" */
#endif

class SkStreamRec {
public:
    SkStreamRec*    fNext;
    SkStream*       fSkStream;

    uint8_t*        memoryBase;           /* font file buffer */
    size_t          size;

    char*           pPath;                /* system path to font file */
    size_t          pathSz;               /* font file path length */

    uint32_t        fRefCnt;
    uint32_t        fFontID;

    static SkStreamRec* ref(uint32_t fontID);
    static void unRef(uint32_t fontID);

private:
    /* assumes ownership of the stream, will call unref() when its done */
    SkStreamRec(SkStream* strm, uint32_t fontID)
        : fSkStream(strm), memoryBase(NULL), fFontID(fontID) {
    }/* end method constructor */

    ~SkStreamRec() {
        if (pPath) {
            free(pPath);
        }/* end if */

        fSkStream->unref();
    }/* end method destructor */
};/* end class SkStreamRec */

class SkScalerContextFEM : public SkScalerContext
{
public:
    SkScalerContextFEM(const SkDescriptor* desc, uint32_t fntId, FontScaler * fs);
    virtual ~SkScalerContextFEM();

protected:
    virtual unsigned generateGlyphCount();
    virtual uint16_t generateCharToGlyph(SkUnichar uni);
    virtual SkUnichar generateGlyphToChar(uint16_t);
    virtual void generateAdvance(SkGlyph* glyph);
    virtual void generateMetrics(SkGlyph* glyph);
    virtual void generateImage(const SkGlyph& glyph);
    virtual void generatePath(const SkGlyph& glyph, SkPath* path);
    virtual void generateFontMetrics(SkPaint::FontMetrics* mx,
                                     SkPaint::FontMetrics* my);
private:
    FontScaler* pFontScaler;
    uint32_t fontID;
};/* end class SkScalerContextFEM */

static SkMutex       gMutexSkFEM;
static SkStreamRec*  gStreamRecHead = NULL;

/* Returns NULL on failure; a valid instance of SkStreamRec otherwise. */
SkStreamRec* SkStreamRec::ref(uint32_t fontID) {
    SkStreamRec* rec = gStreamRecHead;
    while (rec) {
        if (rec->fFontID == fontID) {
            rec->fRefCnt += 1;
            break;
        }/* end if */
        rec = rec->fNext;
    }/* end while */

    if(NULL == rec) {
        SkStream* strm = SkFontHost::OpenStream(fontID);
        if (NULL == strm) {
            SkDEBUGF(("SkFontHost::OpenStream failed opening %x\n", fontID));
        } else {
            /* this passes ownership of strm to the rec */
            rec = SkNEW_ARGS(SkStreamRec, (strm, fontID));
            rec->memoryBase = (uint8_t*)strm->getMemoryBase();
            if (NULL != rec->memoryBase) {
                SK_LOG("memory based stream\n");
                rec->size = strm->getLength();
            } else {
                SK_LOG("callback based stream\n");
                rec->size = strm->read(NULL, 0);
            }/* end else if */

            char* filePath = NULL;
            size_t filePathSz = 0;
            filePathSz = SkFontHost::GetFileName(fontID, NULL, 0, NULL);
            SK_LOG("filePathSz : %d\n", filePathSz);
            if (filePathSz) {
                filePath = (char*)malloc((filePathSz + 1) * sizeof(char));
                SkFontHost::GetFileName(fontID, filePath, filePathSz, NULL);
                filePath[filePathSz] = '\0';
                SK_LOG("filePath : %s\n", filePath);
            }/* end if */
            rec->pPath = filePath;
            rec->pathSz = filePathSz;

            rec->fNext = gStreamRecHead;
            gStreamRecHead = rec;
            rec->fRefCnt = 1;
        }/* end else if */
    }/* end if */

    return rec;
}/* end method ref */

void SkStreamRec::unRef(uint32_t fontID)
{
    SkAutoMutexAcquire  ac(gMutexSkFEM);

    SkStreamRec*  rec = gStreamRecHead;
    SkStreamRec*  prev = NULL;

    while (rec) {
        SkStreamRec* next = rec->fNext;
        if (rec->fFontID == fontID) {
            if (--rec->fRefCnt == 0) {
                if (prev) {
                    prev->fNext = next;
                } else {
                    gStreamRecHead = next;
                }/* end else if */
                SkDELETE(rec);
            }/* end if */
            return;
        }/* end if */
        prev = rec;
        rec = next;
    }/* end while */
    SkASSERT("shouldn't get here, stream not in list");
}/* end method unRef */

SkScalerContextFEM::SkScalerContextFEM(const SkDescriptor* desc, uint32_t fntId, FontScaler * fs)
    : SkScalerContext(desc), fontID(fntId)
{
    pFontScaler = fs;
}/* end method constructor */

SkScalerContextFEM::~SkScalerContextFEM()
{
    delete pFontScaler;
    SkStreamRec::unRef(fontID);
}/* end method destructor */

unsigned SkScalerContextFEM::generateGlyphCount()
{
    return (unsigned)pFontScaler->getGlyphCount();
}/* end method generateGlyphCount */

uint16_t SkScalerContextFEM::generateCharToGlyph(SkUnichar uni)
{
    return pFontScaler->getCharToGlyphID(uni);
}/* end method generateCharToGlyph */

SkUnichar SkScalerContextFEM::generateGlyphToChar(uint16_t glyphID)
{
    return pFontScaler->getGlyphIDToChar(glyphID);
}/* end method generateGlyphToChar */

void SkScalerContextFEM::generateAdvance(SkGlyph* glyph)
{
    FEM16Dot16 fracX = 0, fracY = 0;
    GlyphMetrics gm;

    if (fRec.fFlags & SkScalerContext::kSubpixelPositioning_Flag)
    {
        fracX = glyph->getSubXFixed();
        fracY = glyph->getSubYFixed();
    }/* end if */

    SK_LOG("pFontScaler->getGlyphAdvance for id :%d\n", glyph->getGlyphID(fBaseGlyphCount));

    gm = pFontScaler->getGlyphAdvance(glyph->getGlyphID(fBaseGlyphCount), fracX, fracY);

    glyph->fRsbDelta = (int8_t)gm.rsbDelta;
    glyph->fLsbDelta = (int8_t)gm.lsbDelta;
    glyph->fAdvanceX = (SkFixed)gm.fAdvanceX;
    glyph->fAdvanceY = (SkFixed)gm.fAdvanceY;

    SK_LOG("for id : %d, gm.fAdvanceX : %f, glyph->fAdvanceX :%f\n", glyph->getGlyphID(fBaseGlyphCount), SkFixedToScalar(gm.fAdvanceX), SkFixedToScalar(glyph->fAdvanceX));
}/* end method generateAdvance */

void SkScalerContextFEM::generateMetrics(SkGlyph* glyph)
{
    FEM16Dot16 fracX = 0, fracY = 0;
    GlyphMetrics gm;

    if (fRec.fFlags & SkScalerContext::kSubpixelPositioning_Flag)
    {
        fracX = glyph->getSubXFixed();
        fracY = glyph->getSubYFixed();
    }/* end if */

    SK_LOG("pFontScaler->getGlyphMetrics for id :%d\n", glyph->getGlyphID(fBaseGlyphCount));

    gm = pFontScaler->getGlyphMetrics(glyph->getGlyphID(fBaseGlyphCount), fracX, fracY);

    glyph->fWidth   = (uint16_t)gm.width;
    glyph->fHeight  = (uint16_t)gm.height;
    glyph->fTop     = SkToS16(gm.top);
    glyph->fLeft    = SkToS16(gm.left);

    glyph->fRsbDelta = (int8_t)gm.rsbDelta;
    glyph->fLsbDelta = (int8_t)gm.lsbDelta;
    glyph->fAdvanceX = (SkFixed)gm.fAdvanceX;
    glyph->fAdvanceY = (SkFixed)gm.fAdvanceY;

    SK_LOG("for id : %d, gm.fAdvanceX : %f, glyph->fAdvanceX :%f\n", glyph->getGlyphID(fBaseGlyphCount), SkFixedToScalar(gm.fAdvanceX), SkFixedToScalar(glyph->fAdvanceX));
}/* end method generateMetrics */

void SkScalerContextFEM::generateImage(const SkGlyph& glyph)
{
    FEM16Dot16 fracX = 0, fracY = 0;

    if (fRec.fFlags & SkScalerContext::kSubpixelPositioning_Flag)
    {
        fracX = glyph.getSubXFixed();
        fracY = glyph.getSubYFixed();
    }/* end if */

    SK_LOG("glyph : %d, fracX : %d, fracY : %d, width : %d height : %d rowBytes : %d\n", (uint16_t)glyph.getGlyphID(fBaseGlyphCount), fracX >> 16, fracY >> 16, glyph.fWidth, glyph.fHeight, (uint32_t)glyph.rowBytes());
    pFontScaler->getGlyphImage((uint16_t)glyph.getGlyphID(fBaseGlyphCount), fracX, fracY, (uint32_t)glyph.rowBytes(), glyph.fWidth, glyph.fHeight, reinterpret_cast<uint8_t*>(glyph.fImage));
}/* end method generateImage */

void SkScalerContextFEM::generatePath(const SkGlyph& glyph, SkPath* path)
{
    FEM16Dot16 fracX = 0, fracY = 0;
    GlyphOutline* go = NULL;
    int i = 0;
    FEM26Dot6* x;
    FEM26Dot6* y;
    int16_t* contours;
    uint8_t* onCurve;
    uint8_t*  flags;  /* the points flags */
    FEM26Dot6 startX, startY;
    int16_t contourCount;  /* number of contours in glyph */

    if (fRec.fFlags & SkScalerContext::kSubpixelPositioning_Flag)
    {
        fracX = glyph.getSubXFixed();
        fracY = glyph.getSubYFixed();
    }/* end if */

    go = pFontScaler->getGlyphOutline(glyph.getGlyphID(fBaseGlyphCount), fracX, fracY);

    x = go->x;
    y = go->y;
    contours = go->contours;
    flags = go->flags;
    contourCount = go->contourCount;

    /* convert the outline to a path */
    for (int j = 0; j < contourCount; ++j) {
    int last_point = contours[j];
    FEM26Dot6 cX[4], cY[4];
    int n = 1;

        startX = x[i];
        startY = -y[i];

        if(! (flags[i] & 1) ) {
            startX = (startX + x[last_point]) >> 1;
            startY = (startY + (-y[last_point]) ) >> 1;
        }/* end if */

        /* Reach the first point */
        path->moveTo(SkFixedToScalar(startX << 10), SkFixedToScalar(startY << 10));

        cX[0] = startX;
        cY[0] = startY;
        while (i < last_point) {
            ++i;
            cX[n] = x[i];
            cY[n] = -y[i];
            n++;

            switch (flags[i] & 3) {
            case 2:
                /* cubic bezier element */
                if (n < 4) {
                    continue;
                }/* end if */

                cX[3] = (cX[3] + cX[2])/2;
                cY[3] = (cY[3] + cY[2])/2;

                --i;
                break;
            case 0:
                /* quadratic bezier element */
                if (n < 3) {
                    continue;
                }/* end if */

                cX[3] = (cX[1] + cX[2])/2;
                cY[3] = (cY[1] + cY[2])/2;

                cX[2] = (2*cX[1] + cX[3])/3;
                cY[2] = (2*cY[1] + cY[3])/3;

                cX[1] = (2*cX[1] + cX[0])/3;
                cY[1] = (2*cY[1] + cY[0])/3;

                --i;
                break;
            case 1:
            case 3:
                if (n == 2) {
                    path->lineTo(SkFixedToScalar(cX[1] << 10), SkFixedToScalar(cY[1] << 10));

                    cX[0] = cX[1];
                    cY[0] = cY[1];

                    n = 1;
                    continue;
                } else if (n == 3) {
                    cX[3] = cX[2];
                    cY[3] = cY[2];

                    cX[2] = (2*cX[1] + cX[3])/3;
                    cY[2] = (2*cY[1] + cY[3])/3;

                    cX[1] = (2*cX[1] + cX[0])/3;
                    cX[1] = (2*cY[1] + cY[0])/3;
                }/* end else if*/
                break;
            }/* end switch */
            path->cubicTo(SkFixedToScalar(cX[1] << 10), SkFixedToScalar(cY[1] << 10), SkFixedToScalar(cX[2] << 10), SkFixedToScalar(cY[2] << 10), SkFixedToScalar(cX[3] << 10), SkFixedToScalar(cY[3] << 10));
            cX[0] = cX[3];
            cY[0] = cY[3];
            n = 1;
        }/* end while */

        if (n == 1) {
            path->close();
        } else {
            cX[3] = startX;
            cY[3] = startY;
            if (n == 2) {
                cX[2] = (2*cX[1] + cX[3])/3;
                cY[2] = (2*cY[1] + cY[3])/3;

                cX[1] = (2*cX[1] + cX[0])/3;
                cY[1] = (2*cY[1] + cY[0])/3;
            }/* end if */
            path->cubicTo(SkFixedToScalar(cX[1] << 10), SkFixedToScalar(cY[1] << 10), SkFixedToScalar(cX[2] << 10), SkFixedToScalar(cY[2] << 10), SkFixedToScalar(cX[3] << 10), SkFixedToScalar(cY[3] << 10));
        }/* end else if */
        ++i;
    }/* end for */

    if (go) {
        delete go;
    }/* end if */
}/* end method generatePath */

void SkScalerContextFEM::generateFontMetrics(SkPaint::FontMetrics* mx,
                                             SkPaint::FontMetrics* my)
{
    FontMetrics fmX, fmY;

    FEM16Dot16 fracX = 0, fracY = 0;

    if (mx || my) {
        if (mx && my) {
            pFontScaler->getFontMetrics(&fmX, &fmY);
        } else if(mx) {
            pFontScaler->getFontMetrics(&fmX, NULL);
        } else {
            pFontScaler->getFontMetrics(NULL, &fmY);
        }/* end else if */

        if (mx) {
            mx->fTop = SkFixedToScalar(fmX.fTop);
            mx->fAscent = SkFixedToScalar(fmX.fAscent);
            mx->fDescent = SkFixedToScalar(fmX.fDescent);
            mx->fBottom = SkFixedToScalar(fmX.fBottom);
            mx->fLeading = SkFixedToScalar(fmX.fLeading);

            mx->fAvgCharWidth = SkFixedToScalar(fmX.fAvgCharWidth);
            mx->fXMin = SkFixedToScalar(fmX.fXMin);
            mx->fXMax = SkFixedToScalar(fmX.fXMax);
            mx->fXHeight = SkFixedToScalar(fmX.fXHeight);
        }/* end if */

        if (my) {
            my->fTop = SkFixedToScalar(fmY.fTop);
            my->fAscent = SkFixedToScalar(fmY.fAscent);
            my->fDescent = SkFixedToScalar(fmY.fDescent);
            my->fBottom = SkFixedToScalar(fmY.fBottom);
            my->fLeading = SkFixedToScalar(fmY.fLeading);

            my->fAvgCharWidth = SkFixedToScalar(fmY.fAvgCharWidth);
            my->fXMin = SkFixedToScalar(fmY.fXMin);
            my->fXMax = SkFixedToScalar(fmY.fXMax);
            my->fXHeight = SkFixedToScalar(fmY.fXHeight);
        }/* end if */
    }/* end if */
}/* end method generateFontMetrics */

/*  Export this so that other parts of our FonttHost port can make use of our
    ability to extract the name+style from a stream, using FreeType's api.
*/
SkTypeface::Style find_name_and_attributes(SkStream* stream, SkString* name, bool* isFixedWidth)
{
    SkAutoMutexAcquire  ac(gMutexSkFEM);
    fem::FontStyle fontStyle = fem::STYLE_NORMAL;
    int style = SkTypeface::kNormal;
    size_t fontNameLength = 0;

    const void* buffer = stream->getMemoryBase();
    size_t bufferLength = stream->getLength();

    SK_LOG("bufferLength : %d\n", bufferLength);

    fontNameLength = FontEngineManager::getInstance().getFontNameAndAttribute(buffer, bufferLength, NULL, 0, NULL, NULL);
    SK_LOG("fontNameLength %d\n", fontNameLength);
    if (fontNameLength) {
        char* fontName = (char*)malloc(fontNameLength * sizeof(char));
        if (fontName) {
            fontNameLength = FontEngineManager::getInstance().getFontNameAndAttribute(buffer, bufferLength, fontName, fontNameLength, &fontStyle, isFixedWidth);
            if (fontNameLength) {
                SK_LOG("fontName %s\n", fontName);

                name->set(fontName, fontNameLength);
                SK_LOG("name %s\n", name->c_str());

                free(fontName);

                if (fontStyle & fem::STYLE_BOLD) {
                    style |= SkTypeface::kBold;
                }/* end if */

                if (fontStyle & fem::STYLE_ITALIC) {
                    style |= SkTypeface::kItalic;
                }/* end if */
            }/* end if */
        } else {
            name->reset();
        }/* end else if */
    } else {
        name->reset();
    }/* end else if */

    return (SkTypeface::Style)style;
}/* end method find_name_and_attributes */

SkScalerContext* SkFontHost::CreateScalerContext(const SkDescriptor* desc)
{
    SkAutoMutexAcquire  ac(gMutexSkFEM);
    FontScalerInfo fsInfo;
    FontScaler* fs = NULL;
    SkScalerContext* ctx = NULL;

    SK_LOG("\n");

    const SkScalerContext::Rec* fRec = (const SkScalerContext::Rec*)desc->findEntry(kRec_SkDescriptorTag, NULL);

    if (fRec) {
    SkMatrix m;

    /* load the font file */
    SkStreamRec* fStreamRec = SkStreamRec::ref(fRec->fFontID);

        if (fStreamRec) {
            fsInfo.fontID = fRec->fFontID;

            fsInfo.pPath = fStreamRec->pPath;
            fsInfo.pathSz = fStreamRec->pathSz;

            fsInfo.pBuffer = fStreamRec->memoryBase;
            fsInfo.size = fStreamRec->size;

            fsInfo.subpixelPositioning = fRec->fFlags & SkScalerContext::kSubpixelPositioning_Flag;

            if(SkMask::kBW_Format == fRec->fMaskFormat) {
                fsInfo.maskFormat = fem::ALIAS_MONOCHROME;
            } else if (SkMask::kA8_Format == fRec->fMaskFormat) {
                fsInfo.maskFormat = fem::ALIAS_GRAYSCALE;
            } else if (SkMask::kHorizontalLCD_Format == fRec->fMaskFormat) {
                fsInfo.maskFormat = fem::ALIAS_LCD_H;
            } else if (SkMask::kVerticalLCD_Format == fRec->fMaskFormat) {
                fsInfo.maskFormat = fem::ALIAS_LCD_V;
            } else if (SkMask::kLCD16_Format == fRec->fMaskFormat) {
                fsInfo.maskFormat = fem::ALIAS_LCD16;
            }/* end else if */

            fRec->getSingleMatrix(&m);
            fsInfo.fScaleX = SkScalarToFixed(m.getScaleX());
            fsInfo.fScaleY = SkScalarToFixed(m.getScaleY());
            fsInfo.fSkewX = SkScalarToFixed(m.getSkewX());
            fsInfo.fSkewY = SkScalarToFixed(m.getSkewY());

            fsInfo.flags = 0;

            if (fRec->fFlags & SkScalerContext::kEmbolden_Flag) {
                fsInfo.flags |= fem::Embolden_Flag;
            }/* end if */

            if (fRec->fFlags & SkScalerContext::kEmbeddedBitmapText_Flag) {
                fsInfo.flags |= fem::EmbeddedBitmapText_Flag;
            }/* end if */

            uint8_t h = (uint8_t)fRec->getHinting();
            if (h) {
                fsInfo.flags |=  ((h << 1) & fem::Hinting_Flag);
            }/* end if */

            if (fRec->fFlags & SkScalerContext::kDevKernText_Flag) {
                fsInfo.flags |= fem::DevKernText_Flag;
            }

            fs = FontEngineManager::getInstance().createFontScalerContext(fsInfo);
            if (fs) {
                SK_LOG("font scaler instance created\n");

                /* passing 'fFontID' as to unref it when we are done with scaler context */
                ctx = new SkScalerContextFEM(desc, fStreamRec->fFontID, fs);

                SK_LOG("returning SkScalerContextFEM instance\n");
            } else {
              SK_LOG("failed to create font scaler instance\n");
              SkStreamRec::unRef(fStreamRec->fFontID);
            }/* end else if */
        }/* end if */
    }/* end if */

    return ctx;
}/* end method CreateScalerContext */

#ifdef ANDROID
uint32_t SkFontHost::GetUnitsPerEm(SkFontID fontID)
{
    SkAutoMutexAcquire  ac(gMutexSkFEM);
    uint32_t unitsPerEm = 0;
    SkStream* stream = SkFontHost::OpenStream(fontID);

    if (stream) {
        const void* buffer = stream->getMemoryBase();
        size_t bufferLength = stream->getLength();

        if (buffer && bufferLength) {
            unitsPerEm = FontEngineManager::getInstance().getFontUnitsPerEm(buffer, bufferLength);
        }/* end if */

        stream->unref();
    }/* end if */

    return unitsPerEm;
}/* end method GetUnitsPerEm */
#endif

static bool getWidthAdvance(uint32_t fontID, int gId, int16_t* data) {
    bool retVal = false;
    FEM16Dot16 advance = 0;
    SkStream* stream = SkFontHost::OpenStream(fontID);

    if (stream) {
        const void* buffer = stream->getMemoryBase();
        size_t bufferLength = stream->getLength();

        if (buffer && bufferLength) {
            if (FontEngineManager::getInstance().getGlyphsAdvance(buffer, bufferLength, gId, 1, &advance) == 0) {
                retVal = true;
            }/* end if */

            SkASSERT(data);
            *data = advance;
        }/* end if */

        stream->unref();
    }/* end if */

    return retVal;
}/* end method getWidthAdvance */

// static
SkAdvancedTypefaceMetrics* SkFontHost::GetAdvancedTypefaceMetrics(
        uint32_t fontID,
        SkAdvancedTypefaceMetrics::PerGlyphInfo perGlyphInfo) {
#if defined(SK_BUILD_FOR_MAC) || defined(ANDROID)
    return NULL;
#else
    SkAutoMutexAcquire ac(gFTMutex);

    SkAdvancedTypefaceMetrics* info = NULL;
    SkStream* stream = SkFontHost::OpenStream(fontID);

    if (stream) {
        const void* buffer = stream->getMemoryBase();
        size_t bufferLength = stream->getLength();

        if (buffer && bufferLength) {
            bool cid = false;
            bool canEmbed = false;
            int errCode = 0;

            info = new SkAdvancedTypefaceMetrics;

            if (info) {
               AdvancedTypefaceMetrics* pAdvancedTypefaceMetricsObj = NULL;

               pAdvancedTypefaceMetricsObj = FontEngineManager::getInstance().getAdvancedTypefaceMetrics(buffer, bufferLength);
               if (pAdvancedTypefaceMetricsObj) {
                   info->fFontName.set(pAdvancedTypefaceMetricsObj->pFontName);
                   info->fMultiMaster = pAdvancedTypefaceMetricsObj->fMultiMaster;
                   info->fLastGlyphID = (uint16_t)(pAdvancedTypefaceMetricsObj->fNumGlyphs - 1);
                   info->fEmSize = pAdvancedTypefaceMetricsObj->fEmSize;

                   switch(pAdvancedTypefaceMetricsObj->fType)
                   {
                       case fem::TYPE1_FONT:
                           info->fType = SkAdvancedTypefaceMetrics::kType1_Font;
                           break;
                       case fem::TYPE1CID_FONT:
                           info->fType = SkAdvancedTypefaceMetrics::kType1CID_Font;
                           cid = true;
                           break;
                       case fem::CFF_FONT:
                           info->fType = SkAdvancedTypefaceMetrics::kCFF_Font;
                           break;
                       case fem::TRUETYPE_FONT:
                           info->fType = SkAdvancedTypefaceMetrics::kTrueType_Font;
                           cid = true;
                           break;
                   }/* end switch */

                   info->fStyle = 0;
                   if(pAdvancedTypefaceMetricsObj->fStyle & fem::FIXEDPITCH_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kFixedPitch_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::SERIF_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kSerif_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::SYMBOLIC_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kSymbolic_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::SCRIPT_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kScript_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::NONSYMBOLIC_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kNonsymbolic_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::ITALIC_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kItalic_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::ALLCAPS_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kAllCaps_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::SMALLCAPS_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kSmallCaps_Style;
                   } else if (pAdvancedTypefaceMetricsObj->fStyle & fem::FORCEBOLD_STYLE) {
                       info->fStyle |= SkAdvancedTypefaceMetrics::kForceBold_Style;
                   }/* end else if */

                   info->fItalicAngle = pAdvancedTypefaceMetricsObj->fItalicAngle;

                   info->fAscent = pAdvancedTypefaceMetricsObj->fAscent;
                   info->fDescent = pAdvancedTypefaceMetricsObj->fDescent;
                   info->fStemV = pAdvancedTypefaceMetricsObj->fStemV;
                   info->fCapHeight = pAdvancedTypefaceMetricsObj->fCapHeight;

                   info->fBBox = SkIRect::MakeLTRB(pAdvancedTypefaceMetricsObj->fXMin, pAdvancedTypefaceMetricsObj->fYMax,
                                         pAdvancedTypefaceMetricsObj->fXMax, pAdvancedTypefaceMetricsObj->fYMin);

                   canEmbed = FontEngineManager::canEmbed(buffer, bufferLength);
                   if (!canEmbed || !pAdvancedTypefaceMetricsObj->isScalable ||
                           info->fType == SkAdvancedTypefaceMetrics::kOther_Font) {
                       perGlyphInfo = SkAdvancedTypefaceMetrics::kNo_PerGlyphInfo;
                   }/* end if */


                   if (perGlyphInfo & SkAdvancedTypefaceMetrics::kHAdvance_PerGlyphInfo) {
                       if (pAdvancedTypefaceMetricsObj->fStyle & fem::FIXEDPITCH_STYLE) {
                           int16_t advance = pAdvancedTypefaceMetricsObj->fMaxAdvWidth;
                           appendRange(&info->fGlyphWidths, 0);
                           info->fGlyphWidths->fAdvance.append(1, &advance);
                           finishRange(info->fGlyphWidths.get(), 0, SkAdvancedTypefaceMetrics::WidthRange::kDefault);
                       } else if (!cid) {
                           FEM16Dot16* pGlyphsAdvance = NULL;
                           int gID = 0;
                           int advanceCount = pAdvancedTypefaceMetricsObj->fNumGlyphs + 1;

                           appendRange(&info->fGlyphWidths, 0);

                           pGlyphsAdvance = (FT_Fixed*)malloc(advanceCount * sizeof(FT_Fixed));
                           if (pGlyphsAdvance) {
                               errCode = FontEngineManager::getInstance().getGlyphsAdvance(buffer, bufferLength, gID, advanceCount, pGlyphsAdvance);
                               if (errCode == 0)
                               {
                                   for (int i = 0; i < advanceCount; i++) {
                                       int16_t advance = pGlyphsAdvance[i];
                                       info->fGlyphWidths->fAdvance.append(1, &advance);
                                   }/* end for */
                               }/* end if */
                           } else {
                               errCode = 1;
                           }/* end else if */

                           free(pGlyphsAdvance);

                           finishRange(info->fGlyphWidths.get(), pAdvancedTypefaceMetricsObj->fNumGlyphs - 1, SkAdvancedTypefaceMetrics::WidthRange::kRange);
                       } else {
                           info->fGlyphWidths.reset(getAdvanceData(fontID, pAdvancedTypefaceMetricsObj->fNumGlyphs, &getWidthAdvance));
                       }/* end else if */
                   }/* end if */

                   if (perGlyphInfo & SkAdvancedTypefaceMetrics::kVAdvance_PerGlyphInfo
                           && pAdvancedTypefaceMetricsObj->hasVerticalMetrics) {
                       SkASSERT(false);  // Not implemented yet.
                   }/* end if */


                   if (perGlyphInfo & SkAdvancedTypefaceMetrics::kGlyphNames_PerGlyphInfo &&
                           info->fType == SkAdvancedTypefaceMetrics::kType1_Font) {
                       char** pGlyphsName = NULL;
                       int advanceCount = pAdvancedTypefaceMetricsObj->fNumGlyphs;
                       int gID = 0;

                       pGlyphsName = (char**)malloc(advanceCount * sizeof(char*));
                       if (pGlyphsName) {
                           for (int i = 0; i < advanceCount; i++) {
                               pGlyphsName[i] = (char*)malloc(128 * sizeof(char)); // PS limit for names is 127 bytes.
                               if (pGlyphsName[i] == NULL) {
                                   errCode = 1;
                               }/* end if */
                           }/* end for */

                           if (errCode == 0) {
                               // Postscript fonts may contain more than 255 glyphs, so we end up
                               // using multiple font descriptions with a glyph ordering.  Record
                               // the name of each glyph.
                               info->fGlyphNames.reset(new SkAutoTArray<SkString>(advanceCount));

                               errCode = FontEngineManager::getInstance().getGlyphsName(buffer, bufferLength, gID, advanceCount, pGlyphsName);
                               if (errCode) {
                                   for (int i = 0; i < advanceCount; i++) {
                                       info->fGlyphNames->get()[i + gID].set(pGlyphsName[i]);
                                   }/* end for */
                               }/* end if */
                           }/* end if */

                           for (int i = 0; i < advanceCount; i++) {
                               free(pGlyphsName[i]);
                           }/* end for */
                       }/* end if */

                       free(pGlyphsName);
                   }/* end if */

                   if (perGlyphInfo & SkAdvancedTypefaceMetrics::kToUnicode_PerGlyphInfo &&
                           info->fType != SkAdvancedTypefaceMetrics::kType1_Font &&
                           pAdvancedTypefaceMetricsObj->fNumCharmaps) {
                       int32_t* pGlyphsUnicode = NULL;
                       int gID = 0;
                       int advanceCount = pAdvancedTypefaceMetricsObj->fNumGlyphs

                       if (info->fGlyphToUnicode->isEmpty()) {
                           info->fGlyphToUnicode->setCount(advanceCount);
                           memset(info->fGlyphToUnicode->begin(), 0, sizeof(int32_t*) * advanceCount);
                       }/* end if */

                       pGlyphsUnicode = (int32_t*)malloc(advanceCount * sizeof(int32_t));
                       if (pGlyphsUnicode) {
                           errCode = FontEngineManager::getInstance().getGlyphsUnicode(buffer, bufferLength, gID, advanceCount, pGlyphsUnicode);
                           if (errCode == 0)
                           {
                               for (int i = 0; i < advanceCount; i++) {
                                   info->fGlyphToUnicode[i + gID] = pGlyphsUnicode[i];
                               }/* end for */
                           }/* end if */
                       } else {
                           errCode = 1;
                       }/* end else if */

                       free(pGlyphsUnicode);
                   }/* end if */

                   if (!canEmbed) {
                       info->fType = SkAdvancedTypefaceMetrics::kNotEmbeddable_Font;
                   }/* end if */

                   delete pAdvancedTypefaceMetricsObj;
               } else {
                   errCode = 1;
               }/* end else if */

            }/* end if */

            if (errCode) {
                delete info;
                info = NULL;
            }/* end if */
        }/* end if */

        stream->unref();
    }/* end if */

    return info;
#endif
}/* end method GetAdvancedTypefaceMetrics */

/* TODO: For now this method is not implemented. This can be implemented from
   the FontManager in two ways
   1/ Use the least common properties of all the font engines in the font manager and
      set rec according to that. May not be effective.
   2/ Use the rec of the current active scaler, this can work good, except the
      case where the font in focus is incorrect.
*/
void SkFontHost::FilterRec(SkScalerContext::Rec* rec)
{
}/* end method FilterRec */

