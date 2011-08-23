/* frameworks/base/include/utils/FontEngineManager.cpp
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

#include <utils/FontEngineManager.h>

#include <dlfcn.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>

/* #define FEM_ENABLE_LOG */

#ifdef FEM_ENABLE_LOG
/* fprintf, FILE */
#include <stdio.h>

static FILE * fplog = NULL;

#define FEM_STARTLOG fplog = fopen("/data/femlog.txt", "a");
#define FEM_LOG(...) \
        FEM_STARTLOG \
        fprintf(fplog, __FUNCTION__); \
        fprintf(fplog, ", "); \
        fprintf(fplog, __VA_ARGS__); \
        FEM_ENDLOG
#define FEM_ENDLOG fclose(fplog);
#else
#define FEM_STARTLOG
#define FEM_LOG(...)
#define FEM_ENDLOG
#endif /* FEM_ENABLE_LOG */

#ifdef FEM_UNUSED
#undef FEM_UNUSED
#endif
#define FEM_UNUSED(x) ((void)(x))

#define MAX_PATH_LEN 1024

/* Font engine libraries are decidedly in the system partition. */
#define ANDROID_FONT_ENGINE_PATH "/system/lib/fontengines/"

/* Entry point to font engine plugin */
#define GET_FONT_ENGINE_INSTANCE "getFontEngineInstance"

FontEngineManager* FontEngineManager::pFEMInst = NULL;
typedef int (*direntAlphaSort)(const dirent**, const dirent**);

static int dummyMethod(const struct dirent *unused)
{
	return 1;
}/* end dummyMethod */

GlyphOutline::GlyphOutline(int16_t nOtlnPts, int16_t nContours)
    : contourCount(nContours), pointCount(nOtlnPts),
       x(NULL), y(NULL), contours(NULL), flags(NULL)
{

    x = (FEM26Dot6*)malloc(((pointCount + pointCount) * sizeof(FEM26Dot6)) + (nContours * sizeof(int16_t)) + (pointCount * sizeof(uint8_t)));
    y = (FEM26Dot6*)&x[pointCount];
    contours = (int16_t*)&y[pointCount];
    flags = (uint8_t*)&contours[nContours];
}

GlyphOutline::~GlyphOutline()
{
    free(x);
}

FontEngineManager::FontEngineManager()
    : engineCount(0), pFontEngineList(NULL), pFontEngineInfoArr(NULL)
{
    const char*      path = ANDROID_FONT_ENGINE_PATH;
    struct dirent**  eps;
    int              numEntries;

    numEntries = scandir(path, &eps, dummyMethod, (direntAlphaSort)alphasort);
    if (numEntries >= 0) {
        char  filePath[MAX_PATH_LEN];
        int   length, index;

        memset(filePath, 0, sizeof(filePath));

        length = 0;
        while (path[length]) {
            filePath[length] = path[length];
            length++;
        }

        assert(length < MAX_PATH_LEN);

        pFontEngineInfoArr = (FontEngineInfoArrPtr)calloc(sizeof(FontEngineInfoPtr), numEntries + 1);
        assert(pFontEngineInfoArr);

        for (index = 0; index < numEntries; index ++) {
            int i = length;
            int j = 0;

            while (eps[index]->d_name[j]) {
                filePath[i] = eps[index]->d_name[j];
                i++;
                j++;
            }

            assert(i < MAX_PATH_LEN);

            memset(&filePath[i], 0, MAX_PATH_LEN - i);

            i = i - 3;
            if ( filePath[i] == '.' && filePath[i + 1] == 's' && filePath[i + 2] == 'o' ) {
                void*                      handle = NULL;
                const char*                entryMethodName = GET_FONT_ENGINE_INSTANCE;
                getFontEngineInstanceType  getFontEngineInstancePtr = NULL;
                FontEngine*                inst = NULL;

                FEM_LOG("filePath : %s, engineCount : %d\n", filePath, engineCount);

                handle = dlopen(filePath, RTLD_LAZY);
                getFontEngineInstancePtr = (getFontEngineInstanceType)dlsym(handle, entryMethodName);
                inst = getFontEngineInstancePtr();
                if (inst) {
                    FontEngineNode*  node = (FontEngineNode *)malloc(sizeof(FontEngineNode));
                    node->next = this->pFontEngineList;
                    node->inst = inst;
                    this->pFontEngineList = node;

                    pFontEngineInfoArr[engineCount] = (FontEngineInfoPtr)malloc(sizeof(FontEngineInfo));
                    assert(pFontEngineInfoArr[engineCount]);
                    pFontEngineInfoArr[engineCount]->name = strdup(inst->getName());

                    engineCount++;
                    FEM_LOG("successfully loaded %s font engine, engineCount : %d\n", filePath, engineCount);
                }/* end if */
            }/* end if */
        }/* end for */
    }/* end if */
}/* end method constructor */

FontEngineManager::~FontEngineManager()
{
    register FontEngineInfoArrPtr  pTempFontEngineInfoArr = this->pFontEngineInfoArr;
    register FontEngineInfoPtr     pFontEngineInfo = *pTempFontEngineInfoArr;

    register FontEngineNode*       node = this->pFontEngineList;
    register FontEngineNode*       tempNode = NULL;

    while (pFontEngineInfo) {
        free((void*)pFontEngineInfo->name);
        free(pFontEngineInfo);

        pTempFontEngineInfoArr++;
        pFontEngineInfo = *pTempFontEngineInfoArr;
    }/* end while */

    free(pFontEngineInfoArr);

    while (node) {
        tempNode = node;
        node = node->next;
        free(tempNode);
    }/* end while */
}/* end method destructor */

/* Returns a singleton instance to a font engine manager. */
FontEngineManager& FontEngineManager::getInstance()
{
    if (!pFEMInst)
    {
      FEM_LOG("creating instance\n");
      pFEMInst = new FontEngineManager();
    }

    return *pFEMInst;
}/* end method getInstance */

/*
   FontEngine list is traversed and a request for font scaler is performed
   against a font engine. The API returns immediately if the font scaler
   is successfully created; a request for font scaler creation is made
   to the next font engine in the list otherwise.
*/
FontScaler* FontEngineManager::createFontScalerContext(const FontScalerInfo& desc)
{
    register FontEngineNode*  node = this->pFontEngineList;
    FontScaler*  pFontScalerContext = NULL;

    FEM_LOG("creating font scaler\n");

    while (node != NULL) {
        pFontScalerContext = node->inst->createFontScalerContext(desc);
        if (pFontScalerContext) {
            FEM_LOG("successfully created font scaler\n");
            return pFontScalerContext;
        }/* end if */
        node = node->next;
    }/* end while */

    return NULL;
}/* end method createFontScalerContext */

FontEngine* FontEngineManager::getFontEngine(const char name[])
{
    register FontEngineNode*  node = this->pFontEngineList;

    while (node != NULL) {
        if ( ! strcmp(name, node->inst->getName()) ) {
            return node->inst;
        }/* end if */

        node = node->next;
    }/* end while */

    return NULL;
}/* end method getFontEngine */

size_t FontEngineManager::getFontNameAndAttribute(const char path[], char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth)
{
    register FontEngineNode*  node = this->pFontEngineList;
    size_t count;

    while (node != NULL) {
        count = node->inst->getFontNameAndAttribute(path, name, length, style, isFixedWidth);

        if (count) {
            return count;
        }/* end if */

        node = node->next;
    }/* end while */

    return 0;
}/* end method getFontNameAndAttribute */

size_t FontEngineManager::getFontNameAndAttribute(const void* buffer, const uint32_t bufferLength, char name[], size_t length, fem::FontStyle* style, bool* isFixedWidth)
{
    register FontEngineNode*  node = this->pFontEngineList;
    size_t count;

    while (node != NULL) {
        count = node->inst->getFontNameAndAttribute(buffer, bufferLength, name, length, style, isFixedWidth);

        if (count) {
            return count;
        }/* end if */

        node = node->next;
    }/* end while */

    return 0;
}/* end method getFontNameAndAttribute */

bool FontEngineManager::isFontSupported(const char path[], bool isLoad)
{
    register FontEngineNode*  node = this->pFontEngineList;

    while (node != NULL) {
        if (node->inst->isFontSupported(path, isLoad)) {
            return true;
        }/* end if */

        node = node->next;
    }/* end while */

    return false;
}/* end method isFontSupported */

bool FontEngineManager::isFontSupported(const void* buffer, const uint32_t bufferLength)
{
    register FontEngineNode*  node = this->pFontEngineList;

    while (node != NULL) {
        if (node->inst->isFontSupported(buffer, bufferLength)) {
            return true;
        }/* end if */

        node = node->next;
    }/* end while */

    return false;
}/* end method isFontSupported */

uint32_t FontEngineManager::getFontUnitsPerEm(const char path[])
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t unitsPerEm = 0;

    while (node != NULL) {
        unitsPerEm = node->inst->getFontUnitsPerEm(path);
        if (unitsPerEm) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return unitsPerEm;
}/* end method getFontUnitsPerEm */

uint32_t FontEngineManager::getFontUnitsPerEm(const void* buffer, const uint32_t bufferLength)
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t unitsPerEm = 0;

    while (node != NULL) {
        unitsPerEm = node->inst->getFontUnitsPerEm(buffer, bufferLength);
        if (unitsPerEm) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return unitsPerEm;
}/* end method getFontUnitsPerEm */

bool FontEngineManager::canEmbed(const char path[])
{
    register FontEngineNode*  node = this->pFontEngineList;

    while (node != NULL) {
        if (node->inst->canEmbed(path)) {
            return true;
        }/* end if */

        node = node->next;
    }/* end while */

    return false;
}/* end method canEmbed */

bool FontEngineManager::canEmbed(const void* buffer, const uint32_t bufferLength)
{
    register FontEngineNode*  node = this->pFontEngineList;

    while (node != NULL) {
        if (node->inst->canEmbed(buffer, bufferLength)) {
            return true;
        }/* end if */

        node = node->next;
    }/* end while */

    return false;
}/* end method canEmbed */

uint32_t FontEngineManager::getGlyphsAdvance(const char path[], uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance)
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t errCode = 0;

    while (node != NULL) {
        errCode = node->inst->getGlyphsAdvance(path, start, count, pGlyphsAdvance);
        if (errCode == 0) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return errCode;
}/* end method getGlyphsAdvance */

uint32_t FontEngineManager::getGlyphsAdvance(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, FEM16Dot16* pGlyphsAdvance)
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t errCode = 0;

    while (node != NULL) {
        errCode = node->inst->getGlyphsAdvance(buffer, bufferLength, start, count, pGlyphsAdvance);
        if (errCode == 0) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return errCode;
}/* end method getGlyphsAdvance */

uint32_t FontEngineManager::getGlyphsName(const char path[], uint32_t start, uint32_t count, char** pGlyphsName)
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t errCode = 0;

    while (node != NULL) {
        errCode = node->inst->getGlyphsName(path, start, count, pGlyphsName);
        if (errCode == 0) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return errCode;
}/* end method getGlyphsName */

uint32_t FontEngineManager::getGlyphsName(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, char** pGlyphsName)
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t errCode = 0;

    while (node != NULL) {
        errCode = node->inst->getGlyphsName(buffer, bufferLength, start, count, pGlyphsName);
        if (errCode == 0) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return errCode;
}/* end method getGlyphsName */

uint32_t FontEngineManager::getGlyphsUnicode(const char path[], uint32_t start, uint32_t count, int32_t* pGlyphsUnicode)
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t errCode = 0;

    while (node != NULL) {
        errCode = node->inst->getGlyphsUnicode(path, start, count, pGlyphsUnicode);
        if (errCode == 0) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return errCode;
}/* end method getGlyphsUnicode */

uint32_t FontEngineManager::getGlyphsUnicode(const void* buffer, const uint32_t bufferLength, uint32_t start, uint32_t count, int32_t* pGlyphsUnicode)
{
    register FontEngineNode*  node = this->pFontEngineList;
    uint32_t errCode = 0;

    while (node != NULL) {
        errCode = node->inst->getGlyphsUnicode(buffer, bufferLength, start, count, pGlyphsUnicode);
        if (errCode == 0) {
            break;
        }/* end if */

        node = node->next;
    }/* end while */

    return errCode;
}/* end method getGlyphsUnicode */

AdvancedTypefaceMetrics* FontEngineManager::getAdvancedTypefaceMetrics(const char path[])
{
    register FontEngineNode*  node = this->pFontEngineList;
    AdvancedTypefaceMetrics*  pAdvancedTypefaceMetrics = NULL;

    FEM_LOG("creating AdvancedTypefaceMetrics\n");

    while (node != NULL) {
        pAdvancedTypefaceMetrics = node->inst->getAdvancedTypefaceMetrics(path);
        if (pAdvancedTypefaceMetrics) {
            FEM_LOG("successfully created AdvancedTypefaceMetrics\n");
            return pAdvancedTypefaceMetrics;
        }/* end if */
        node = node->next;
    }/* end while */

    return NULL;
}/* end method getAdvanceTypeFaceMetrics */

AdvancedTypefaceMetrics* FontEngineManager::getAdvancedTypefaceMetrics(const void* buffer, const uint32_t bufferLength)
{
    register FontEngineNode*  node = this->pFontEngineList;
    AdvancedTypefaceMetrics*  pAdvancedTypefaceMetrics = NULL;

    FEM_LOG("creating AdvancedTypefaceMetrics\n");

    while (node != NULL) {
        pAdvancedTypefaceMetrics = node->inst->getAdvancedTypefaceMetrics(buffer, bufferLength);
        if (pAdvancedTypefaceMetrics) {
            FEM_LOG("successfully created AdvancedTypefaceMetrics\n");
            return pAdvancedTypefaceMetrics;
        }/* end if */
        node = node->next;
    }/* end while */

    return NULL;
}/* end method getAdvancedTypefaceMetrics */
