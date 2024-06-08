#include <tiny3d.h>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define UNICODE
#define COBJMACROS
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include <vssym32.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <shlguid.h>
#include <commdlg.h>
#include <shlwapi.h>

#include <GL/gl.h>

#include <mmdeviceapi.h>
#include <audioclient.h>

#include <d3d11.h>
#include <d3dcompiler.h>

static IDXGISwapChain *swapChain;
static ID3D11Device *device;
static ID3D11DeviceContext *deviceContext;
static ID3D11RenderTargetView *renderTargetView;
static ID3D11DepthStencilView *depthStencilView;
static ID3D11Texture2D *texture;
static ID3D11ShaderResourceView *textureView;
static ID3D11SamplerState *sampler;
static ID3D11BlendState *blendState;
static ID3D11RasterizerState *rasterizerState;
static ID3D11InputLayout *layout;
static ID3D11VertexShader *vshader;
static ID3D11PixelShader *pshader;
static ID3D11DepthStencilState *depthStencilState;
typedef struct {
    float position[2];
    float texcoord[2];
} Vertex;

static const GUID _CLSID_MMDeviceEnumerator = {0xbcde0395, 0xe52f, 0x467c, {0x8e,0x3d, 0xc4,0x57,0x92,0x91,0x69,0x2e}};
static const GUID _IID_IMMDeviceEnumerator = {0xa95664d2, 0x9614, 0x4f35, {0xa7,0x46, 0xde,0x8d,0xb6,0x36,0x17,0xe6}};
static const GUID _IID_IAudioClient = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1,0x78, 0xc2,0xf5,0x68,0xa7,0x03,0xb2}};
static const GUID _KSDATAFORMAT_SUBTYPE_IEEE_FLOAT = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID _KSDATAFORMAT_SUBTYPE_PCM = {0x00000001,0x0000,0x0010,{0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
static const GUID _IID_IAudioRenderClient = {0xf294acfc, 0x3146, 0x4483, {0xa7, 0xbf, 0xad, 0xdc, 0xa7, 0xc2, 0x60, 0xe2}};
static IMMDeviceEnumerator *enu = NULL;
static IMMDevice *dev = NULL;
static IAudioClient *client = NULL;
static IAudioRenderClient* renderClient = NULL;

static bool coInitCalled = false;
static bool mfStartupCalled = false;
void safe_coinit(void){
    if (!coInitCalled){
        ASSERT_FILE(SUCCEEDED(CoInitialize(0)));
        coInitCalled = true;
    }
}
void safe_mfstartup(void){
    if (!mfStartupCalled){
        ASSERT_FILE(SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET)));
        mfStartupCalled = true;
    }
}

void error_box(char *msg){
    MessageBoxA(0,msg,"Error",MB_ICONERROR);
}

uint32_t *load_image(bool flip_vertically, int *width, int *height, char *format, ...){
    va_list args;
    va_start(args,format);
    assertPath = local_path_to_absolute_vararg(format,args);

    safe_coinit();

    static IWICImagingFactory2 *ifactory = 0;
    if (!ifactory){
        ASSERT(SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory2,0,CLSCTX_INPROC_SERVER,&IID_IWICImagingFactory2,&ifactory)));
        ASSERT(ifactory);
    }
    IWICBitmapDecoder *pDecoder = 0;
    size_t len = strlen(assertPath)+1;
    WCHAR *wpath = malloc(len*sizeof(*wpath));
    ASSERT(wpath);
    mbstowcs(wpath,assertPath,len);
    ASSERT_FILE(SUCCEEDED(ifactory->lpVtbl->CreateDecoderFromFilename(ifactory,wpath,0,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&pDecoder)));
    free(wpath);
    IWICBitmapFrameDecode *pFrame = 0;
    ASSERT_FILE(SUCCEEDED(pDecoder->lpVtbl->GetFrame(pDecoder,0,&pFrame)));
    IWICBitmapSource *convertedSrc = 0;
    ASSERT_FILE(SUCCEEDED(WICConvertBitmapSource(&GUID_WICPixelFormat32bppRGBA,(IWICBitmapSource *)pFrame,&convertedSrc)));
    ASSERT_FILE(SUCCEEDED(convertedSrc->lpVtbl->GetSize(convertedSrc,width,height)));
    uint32_t size = width[0]*height[0]*sizeof(uint32_t);
    UINT rowPitch = width[0]*sizeof(uint32_t);
    uint32_t *pixels = malloc(size);
    ASSERT_FILE(pixels);
    if (flip_vertically){
        IWICBitmapFlipRotator *pFlipRotator;
        ASSERT_FILE(SUCCEEDED(ifactory->lpVtbl->CreateBitmapFlipRotator(ifactory,&pFlipRotator)));
        ASSERT_FILE(SUCCEEDED(pFlipRotator->lpVtbl->Initialize(pFlipRotator,convertedSrc,WICBitmapTransformFlipVertical)));
        ASSERT_FILE(SUCCEEDED(pFlipRotator->lpVtbl->CopyPixels(pFlipRotator,0,rowPitch,size,(BYTE *)pixels)));
        pFlipRotator->lpVtbl->Release(pFlipRotator);
    } else {
        ASSERT(SUCCEEDED(convertedSrc->lpVtbl->CopyPixels(convertedSrc,0,rowPitch,size,(BYTE *)pixels)));
    }
    convertedSrc->lpVtbl->Release(convertedSrc);
    pFrame->lpVtbl->Release(pFrame);
    pDecoder->lpVtbl->Release(pDecoder);
    
    va_end(args);

    return pixels;
}

int16_t *load_audio(int *nFrames, char *format, ...){
    va_list args;
    va_start(args,format);
    assertPath = local_path_to_absolute_vararg(format,args);

    IMFSourceReader *pReader = NULL;
    
    safe_coinit();

    safe_mfstartup();

    {
        size_t len = strlen(assertPath)+1;
        WCHAR *wpath = malloc(len*sizeof(*wpath));
        ASSERT(wpath);
        mbstowcs(wpath,assertPath,len);
        ASSERT_FILE(SUCCEEDED(MFCreateSourceReaderFromURL(wpath, NULL, &pReader)));
        free(wpath);
    }

    IMFMediaType *pUncompressedAudioType = NULL;
    IMFMediaType *pPartialType = NULL;

    // Select the first audio stream, and deselect all other streams.
    ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->SetStreamSelection(pReader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE)));
    ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->SetStreamSelection(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE)));

    // Create a partial media type that specifies uncompressed PCM audio.
    ASSERT_FILE(SUCCEEDED(MFCreateMediaType(&pPartialType)));
    ASSERT_FILE(SUCCEEDED(pPartialType->lpVtbl->SetGUID(pPartialType, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio)));
    ASSERT_FILE(SUCCEEDED(pPartialType->lpVtbl->SetGUID(pPartialType, &MF_MT_SUBTYPE, &MFAudioFormat_PCM)));
    ASSERT_FILE(SUCCEEDED(pPartialType->lpVtbl->SetUINT32(pPartialType, &MF_MT_AUDIO_SAMPLES_PER_SECOND, TINY3D_SAMPLE_RATE)));
    ASSERT_FILE(SUCCEEDED(pPartialType->lpVtbl->SetUINT32(pPartialType, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16)));
    ASSERT_FILE(SUCCEEDED(pPartialType->lpVtbl->SetUINT32(pPartialType, &MF_MT_AUDIO_NUM_CHANNELS, 2)));

    // Set this type on the source reader. The source reader will
    // load the necessary decoder.
    ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->SetCurrentMediaType(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pPartialType)));

    // Get the complete uncompressed format.
    ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->GetCurrentMediaType(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pUncompressedAudioType)));

    // Ensure the stream is selected.
    ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->SetStreamSelection(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE)));

    int nSamples = 0;
    DWORD cbAudioData = 0;
    DWORD cbBuffer = 0;
    BYTE *pAudioData = NULL;
    IMFSample *pSample = NULL;
    IMFMediaBuffer *pBuffer = NULL;
    while (1){
        DWORD dwFlags = 0;
        ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->ReadSample(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &dwFlags, NULL, &pSample)));

        if (dwFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            fatal_error("Type change - not supported by WAVE file format.\n");
            break;
        }
        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            break;
        }

        if (pSample == NULL)
        {
            continue;
        }

        DWORD totalLen;
        ASSERT_FILE(SUCCEEDED(pSample->lpVtbl->GetTotalLength(pSample,&totalLen)));

        nSamples += totalLen/sizeof(int16_t);
    }

    PROPVARIANT var;
    var.vt = VT_I8;
    var.hVal.QuadPart = 0;
    ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->SetCurrentPosition(pReader,&GUID_NULL,&var)));
    int16_t *out = malloc(nSamples*sizeof(*out));
    ASSERT_FILE(out);
    BYTE *outp = (BYTE *)out;

    *nFrames = nSamples/2;

    while (1){
        DWORD dwFlags = 0;
        ASSERT_FILE(SUCCEEDED(pReader->lpVtbl->ReadSample(pReader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, NULL, &dwFlags, NULL, &pSample)));

        if (dwFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            fatal_error("Type change - not supported by WAVE file format.\n");
            break;
        }
        if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            break;
        }

        if (pSample == NULL)
        {
            continue;
        }

        ASSERT_FILE(SUCCEEDED(pSample->lpVtbl->ConvertToContiguousBuffer(pSample, &pBuffer)));

        ASSERT_FILE(SUCCEEDED(pBuffer->lpVtbl->Lock(pBuffer, &pAudioData, NULL, &cbBuffer)));

        memcpy(outp,pAudioData,cbBuffer);
        outp += cbBuffer;
        
        ASSERT_FILE(SUCCEEDED(pBuffer->lpVtbl->Unlock(pBuffer)));

        pBuffer->lpVtbl->Release(pBuffer);
        pSample->lpVtbl->Release(pSample);
    }

    pUncompressedAudioType->lpVtbl->Release(pUncompressedAudioType);
    pPartialType->lpVtbl->Release(pPartialType);

    pReader->lpVtbl->Release(pReader);

    va_end(args);

    return out;
}

wchar_t *get_keyboard_layout_name(){
    char name[KL_NAMELENGTH+2];
    GetKeyboardLayoutNameA(name+2);
    name[0] = '0';
    name[1] = 'X';
    int code;
    StrToIntExA(name,STIF_SUPPORT_HEX,&code);
    switch (code){
        case 0x00140C00: return L"ADLaM";
        case 0x0000041C: return L"Albanian";
        case 0x00000401: return L"Arabic (101)";
        case 0x00010401: return L"Arabic (102)";
        case 0x00020401: return L"Arabic (102) AZERTY";
        case 0x0000042B: return L"Armenian Eastern (Legacy)";
        case 0x0002042B: return L"Armenian Phonetic";
        case 0x0003042B: return L"Armenian Typewriter";
        case 0x0001042B: return L"Armenian Western (Legacy)";
        case 0x0000044D: return L"Assamese - INSCRIPT";
        case 0x0001042C: return L"Azerbaijani (Standard)";
        case 0x0000082C: return L"Azerbaijani Cyrillic";
        case 0x0000042C: return L"Azerbaijani Latin";
        case 0x00000445: return L"Bangla";
        case 0x00020445: return L"Bangla - INSCRIPT";
        case 0x00010445: return L"Bangla - INSCRIPT (Legacy)";
        case 0x0000046D: return L"Bashkir";
        case 0x00000423: return L"Belarusian";
        case 0x0001080C: return L"Belgian (Comma)";
        case 0x00000813: return L"Belgian (Period)";
        case 0x0000080C: return L"Belgian French";
        case 0x0000201A: return L"Bosnian (Cyrillic)";
        case 0x000B0C00: return L"Buginese";
        case 0x00030402: return L"Bulgarian";
        case 0x00010402: return L"Bulgarian (Latin)";
        case 0x00040402: return L"Bulgarian (Phonetic Traditional)";
        case 0x00020402: return L"Bulgarian (Phonetic)";
        case 0x00000402: return L"Bulgarian (Typewriter)";
        case 0x00001009: return L"Canadian French";
        case 0x00000C0C: return L"Canadian French (Legacy)";
        case 0x00011009: return L"Canadian Multilingual Standard";
        case 0x0000085F: return L"Central Atlas Tamazight";
        case 0x00000492: return L"Central Kurdish";
        case 0x0000045C: return L"Cherokee Nation";
        case 0x0001045C: return L"Cherokee Phonetic";
        case 0x00000804: return L"Chinese (Simplified) - US";
        case 0x00001004: return L"Chinese (Simplified, Singapore) - US";
        case 0x00000404: return L"Chinese (Traditional) - US";
        case 0x00000C04: return L"Chinese (Traditional, Hong Kong S.A.R.) - US";
        case 0x00001404: return L"Chinese (Traditional, Macao S.A.R.) - US";
        case 0x00000405: return L"Czech";
        case 0x00010405: return L"Czech (QWERTY)";
        case 0x00020405: return L"Czech Programmers";
        case 0x00000406: return L"Danish";
        case 0x00000439: return L"Devanagari - INSCRIPT";
        case 0x00000465: return L"Divehi Phonetic";
        case 0x00010465: return L"Divehi Typewriter";
        case 0x00000413: return L"Dutch";
        case 0x00000C51: return L"Dzongkha";
        case 0x00004009: return L"English (India)";
        case 0x00000425: return L"Estonian";
        case 0x00000438: return L"Faeroese";
        case 0x0000040B: return L"Finnish";
        case 0x0001083B: return L"Finnish with Sami";
        case 0x0000040C: return L"French";
        case 0x00120C00: return L"Futhark";
        case 0x00020437: return L"Georgian (Ergonomic)";
        case 0x00000437: return L"Georgian (Legacy)";
        case 0x00030437: return L"Georgian (MES)";
        case 0x00040437: return L"Georgian (Old Alphabets)";
        case 0x00010437: return L"Georgian (QWERTY)";
        case 0x00000407: return L"German";
        case 0x00010407: return L"German (IBM)";
        case 0x000C0C00: return L"Gothic";
        case 0x00000408: return L"Greek";
        case 0x00010408: return L"Greek (220)";
        case 0x00030408: return L"Greek (220) Latin";
        case 0x00020408: return L"Greek (319)";
        case 0x00040408: return L"Greek (319) Latin";
        case 0x00050408: return L"Greek Latin";
        case 0x00060408: return L"Greek Polytonic";
        case 0x0000046F: return L"Greenlandic";
        case 0x00000474: return L"Guarani";
        case 0x00000447: return L"Gujarati";
        case 0x00000468: return L"Hausa";
        case 0x00000475: return L"Hawaiian";
        case 0x0000040D: return L"Hebrew";
        case 0x0002040D: return L"Hebrew (Standard)";
        case 0x00010439: return L"Hindi Traditional";
        case 0x0000040E: return L"Hungarian";
        case 0x0001040E: return L"Hungarian 101-key";
        case 0x0000040F: return L"Icelandic";
        case 0x00000470: return L"Igbo";
        case 0x0000085D: return L"Inuktitut - Latin";
        case 0x0001045D: return L"Inuktitut - Naqittaut";
        case 0x00001809: return L"Irish";
        case 0x00000410: return L"Italian";
        case 0x00010410: return L"Italian (142)";
        case 0x00000411: return L"Japanese";
        case 0x00110C00: return L"Javanese";
        case 0x0000044B: return L"Kannada";
        case 0x0000043F: return L"Kazakh";
        case 0x00000453: return L"Khmer";
        case 0x00010453: return L"Khmer (NIDA)";
        case 0x00000412: return L"Korean";
        case 0x00000440: return L"Kyrgyz Cyrillic";
        case 0x00000454: return L"Lao";
        case 0x0000080A: return L"Latin American";
        case 0x00000426: return L"Latvian";
        case 0x00010426: return L"Latvian (QWERTY)";
        case 0x00020426: return L"Latvian (Standard)";
        case 0x00070C00: return L"Lisu (Basic)";
        case 0x00080C00: return L"Lisu (Standard)";
        case 0x00010427: return L"Lithuanian";
        case 0x00000427: return L"Lithuanian IBM";
        case 0x00020427: return L"Lithuanian Standard";
        case 0x0000046E: return L"Luxembourgish";
        case 0x0000042F: return L"Macedonian";
        case 0x0001042F: return L"Macedonian - Standard";
        case 0x0000044C: return L"Malayalam";
        case 0x0000043A: return L"Maltese 47-Key";
        case 0x0001043A: return L"Maltese 48-Key";
        case 0x00000481: return L"Maori";
        case 0x0000044E: return L"Marathi";
        case 0x00000850: return L"Mongolian (Mongolian Script)";
        case 0x00000450: return L"Mongolian Cyrillic";
        case 0x00010C00: return L"Myanmar (Phonetic order)";
        case 0x00130C00: return L"Myanmar (Visual order)";
        case 0x00001409: return L"NZ Aotearoa";
        case 0x00000461: return L"Nepali";
        case 0x00020C00: return L"New Tai Lue";
        case 0x00000414: return L"Norwegian";
        case 0x0000043B: return L"Norwegian with Sami";
        case 0x00090C00: return L"N’Ko";
        case 0x00000448: return L"Odia";
        case 0x00040C00: return L"Ogham";
        case 0x000D0C00: return L"Ol Chiki";
        case 0x000F0C00: return L"Old Italic";
        case 0x00150C00: return L"Osage";
        case 0x000E0C00: return L"Osmanya";
        case 0x00000463: return L"Pashto (Afghanistan)";
        case 0x00000429: return L"Persian";
        case 0x00050429: return L"Persian (Standard)";
        case 0x000A0C00: return L"Phags-pa";
        case 0x00010415: return L"Polish (214)";
        case 0x00000415: return L"Polish (Programmers)";
        case 0x00000816: return L"Portuguese";
        case 0x00000416: return L"Portuguese (Brazil ABNT)";
        case 0x00010416: return L"Portuguese (Brazil ABNT2)";
        case 0x00000446: return L"Punjabi";
        case 0x00000418: return L"Romanian (Legacy)";
        case 0x00020418: return L"Romanian (Programmers)";
        case 0x00010418: return L"Romanian (Standard)";
        case 0x00000419: return L"Russian";
        case 0x00010419: return L"Russian (Typewriter)";
        case 0x00020419: return L"Russian - Mnemonic";
        case 0x00000485: return L"Sakha";
        case 0x0002083B: return L"Sami Extended Finland-Sweden";
        case 0x0001043B: return L"Sami Extended Norway";
        case 0x00011809: return L"Scottish Gaelic";
        case 0x00000C1A: return L"Serbian (Cyrillic)";
        case 0x0000081A: return L"Serbian (Latin)";
        case 0x0000046C: return L"Sesotho sa Leboa";
        case 0x00000432: return L"Setswana";
        case 0x0000045B: return L"Sinhala";
        case 0x0001045B: return L"Sinhala - Wij 9";
        case 0x0000041B: return L"Slovak";
        case 0x0001041B: return L"Slovak (QWERTY)";
        case 0x00000424: return L"Slovenian";
        case 0x00100C00: return L"Sora";
        case 0x0001042E: return L"Sorbian Extended";
        case 0x0002042E: return L"Sorbian Standard";
        case 0x0000042E: return L"Sorbian Standard (Legacy)";
        case 0x0000040A: return L"Spanish";
        case 0x0001040A: return L"Spanish Variation";
        case 0x0000041A: return L"Standard";
        case 0x0000041D: return L"Swedish";
        case 0x0000083B: return L"Swedish with Sami";
        case 0x0000100C: return L"Swiss French";
        case 0x00000807: return L"Swiss German";
        case 0x0000045A: return L"Syriac";
        case 0x0001045A: return L"Syriac Phonetic";
        case 0x00030C00: return L"Tai Le";
        case 0x00000428: return L"Tajik";
        case 0x00000449: return L"Tamil";
        case 0x00020449: return L"Tamil 99";
        case 0x00030449: return L"Tamil Anjal";
        case 0x00010444: return L"Tatar";
        case 0x00000444: return L"Tatar (Legacy)";
        case 0x0000044A: return L"Telugu";
        case 0x0000041E: return L"Thai Kedmanee";
        case 0x0002041E: return L"Thai Kedmanee (non-ShiftLock)";
        case 0x0001041E: return L"Thai Pattachote";
        case 0x0003041E: return L"Thai Pattachote (non-ShiftLock)";
        case 0x00000451: return L"Tibetan (PRC)";
        case 0x00010451: return L"Tibetan (PRC) - Updated";
        case 0x0000105F: return L"Tifinagh (Basic)";
        case 0x0001105F: return L"Tifinagh (Extended)";
        case 0x00010850: return L"Traditional Mongolian (Standard)";
        case 0x0001041F: return L"Turkish F";
        case 0x0000041F: return L"Turkish Q";
        case 0x00000442: return L"Turkmen";
        case 0x00000409: return L"US";
        case 0x00050409: return L"US English Table for IBM Arabic 238_L";
        case 0x00000422: return L"Ukrainian";
        case 0x00020422: return L"Ukrainian (Enhanced)";
        case 0x00000809: return L"United Kingdom";
        case 0x00000452: return L"United Kingdom Extended";
        case 0x00010409: return L"United States-Dvorak";
        case 0x00030409: return L"United States-Dvorak for left hand";
        case 0x00040409: return L"United States-Dvorak for right hand";
        case 0x00020409: return L"United States-International";
        case 0x00000420: return L"Urdu";
        case 0x00010480: return L"Uyghur";
        case 0x00000480: return L"Uyghur (Legacy)";
        case 0x00000843: return L"Uzbek Cyrillic";
        case 0x0000042A: return L"Vietnamese";
        case 0x00000488: return L"Wolof";
        case 0x0000046A: return L"Yoruba";
        default: return L"Unknown";
    }
}

void get_key_text(int scancode, wchar_t *buf, int bufcount){
    GetKeyNameTextW(scancode<<16,buf,bufcount);
}

typedef struct {
	BITMAPINFOHEADER    bmiHeader;
	RGBQUAD             bmiColors[4];
} BITMAPINFO_TRUECOLOR32;

static struct GdiImage{
	int width, height;
	unsigned char *pixels;
	HDC hdcBmp;
	HFONT fontOld;
    int fontHeight;
    char fontName[MAX_PATH];
} gdiImg = {.fontHeight = 12};

static void ensure_hdcBmp(){
    if (!gdiImg.hdcBmp){
        HDC hdcScreen = GetDC(0);
        gdiImg.hdcBmp = CreateCompatibleDC(hdcScreen);
        ReleaseDC(0,hdcScreen);
        SetBkMode(gdiImg.hdcBmp,TRANSPARENT);
    }
}
void text_set_target_image(uint32_t *pixels, int width, int height){
    gdiImg.width = width;
    gdiImg.height = height;
    gdiImg.pixels = (unsigned char *)pixels;

    ensure_hdcBmp();
}
static void text_update_font(){
    HFONT font = CreateFontA(-gdiImg.fontHeight,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY, VARIABLE_PITCH,gdiImg.fontName);

    ensure_hdcBmp();

    HFONT old = SelectObject(gdiImg.hdcBmp,font);
	if (!gdiImg.fontOld){
		gdiImg.fontOld = old;
	} else {
        DeleteObject(old);
    }
}
void text_set_font(char *ttfPathFormat, ...){
    va_list args;
	va_start(args,ttfPathFormat);
	char *path = local_path_to_absolute_vararg(ttfPathFormat,args);
	va_end(args);
    ASSERT(1 == AddFontResourceExA(path,FR_PRIVATE,NULL));
    get_font_name(path,gdiImg.fontName,COUNT(gdiImg.fontName));
    text_update_font();
}
void text_set_font_height(int height){
    gdiImg.fontHeight = height;
    text_update_font();
}
void text_set_color(float r, float g, float b){
    SetTextColor(gdiImg.hdcBmp,RGB((int)(255*r),(int)(255*g),(int)(255*b)));
}
void text_draw(int left, int right, int bottom, int top, char *str){
    BITMAPINFO_TRUECOLOR32 bmi = {
		.bmiHeader.biSize = sizeof(BITMAPINFOHEADER),
		.bmiHeader.biWidth = gdiImg.width,
		.bmiHeader.biHeight = gdiImg.height,
		.bmiHeader.biPlanes = 1,
		.bmiHeader.biCompression = BI_RGB | BI_BITFIELDS,
		.bmiHeader.biBitCount = 32,
		.bmiColors[0].rgbRed = 0xff,
		.bmiColors[1].rgbGreen = 0xff,
		.bmiColors[2].rgbBlue = 0xff,
	};
    HBITMAP hbm, hbmOld;
    unsigned char *pixels;
    hbm = CreateDIBSection(gdiImg.hdcBmp,(BITMAPINFO *)&bmi,DIB_RGB_COLORS,&pixels,0,0);
	ASSERT(hbm);
    for (size_t i = 0; i < gdiImg.width*gdiImg.height; i++){
        pixels[i*4+0] = gdiImg.pixels[i*4+2];
        pixels[i*4+1] = gdiImg.pixels[i*4+1];
        pixels[i*4+2] = gdiImg.pixels[i*4+0];
    }
	hbmOld = SelectObject(gdiImg.hdcBmp,hbm);

    RECT r = {
        .left = left,
        .right = right,
        .bottom = gdiImg.height-bottom,
        .top = gdiImg.height-top
    };
    DrawTextA(gdiImg.hdcBmp,str,-1,&r,DT_LEFT|DT_NOPREFIX|DT_EXPANDTABS);
    GdiFlush();

    for (size_t i = 0; i < gdiImg.width*gdiImg.height; i++){
        gdiImg.pixels[i*4+0] = pixels[i*4+2];
        gdiImg.pixels[i*4+1] = pixels[i*4+1];
        gdiImg.pixels[i*4+2] = pixels[i*4+0];
    }

    SelectObject(gdiImg.hdcBmp,hbmOld);
    DeleteObject(hbm);
}

HWND gwnd;
void captureMouse(){
    RECT r;
    GetClientRect(gwnd,&r);
    ClientToScreen(gwnd,(POINT*)&r.left);
    ClientToScreen(gwnd,(POINT*)&r.right);
    ClipCursor(&r);
    ShowCursor(0);
}
void releaseMouse(){
    RECT r;
    GetClientRect(gwnd,&r);
    POINT p = {r.right/2,r.bottom/2};
    ClientToScreen(gwnd,&p);
    SetCursorPos(p.x,p.y);
    ClipCursor(NULL);
    ShowCursor(1);
}

static bool mouse_is_locked = false;
bool is_mouse_locked(void){
	return mouse_is_locked;
}
void lock_mouse(bool locked){
	mouse_is_locked = locked;
	if (locked){
		captureMouse();
	} else {
        releaseMouse();
	}
}

void toggle_fullscreen(){

}

float get_dpi_scale(){
    return 1.0f;
}

static image_t *screen;

void CreateRenderTargets(){
    ID3D11Texture2D *backBuffer = 0;
    ASSERT(SUCCEEDED(swapChain->lpVtbl->GetBuffer(swapChain, 0, &IID_ID3D11Texture2D, (void**)&backBuffer)));

    ASSERT(SUCCEEDED(device->lpVtbl->CreateRenderTargetView(device, (ID3D11Resource *)backBuffer, 0, &renderTargetView)));

    D3D11_TEXTURE2D_DESC depthStencilDesc;
    backBuffer->lpVtbl->GetDesc(backBuffer, &depthStencilDesc);
    backBuffer->lpVtbl->Release(backBuffer);

    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    ID3D11Texture2D* depthStencil;

    ASSERT(SUCCEEDED(device->lpVtbl->CreateTexture2D(device, &depthStencilDesc, 0, &depthStencil)));
    ASSERT(SUCCEEDED(device->lpVtbl->CreateDepthStencilView(device, (ID3D11Resource *)depthStencil, 0, &depthStencilView)));

    depthStencil->lpVtbl->Release(depthStencil);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){
    switch(msg){
        case WM_CREATE:{
            DWORD darkTitlebar = 1;
            int DwmwaUseImmersiveDarkMode = 20,
                DwmwaUseImmersiveDarkModeBefore20h1 = 19;
            SUCCEEDED(DwmSetWindowAttribute(hwnd, DwmwaUseImmersiveDarkMode, &darkTitlebar, sizeof(darkTitlebar))) ||
                SUCCEEDED(DwmSetWindowAttribute(hwnd, DwmwaUseImmersiveDarkModeBefore20h1, &darkTitlebar, sizeof(darkTitlebar)));

            RECT wr;
            GetWindowRect(hwnd,&wr);
            SetWindowPos(hwnd,0,wr.left,wr.top,wr.right-wr.left,wr.bottom-wr.top,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE); //prevent initial white frame

            UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef _DEBUG
            creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
            D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0};
            DXGI_SWAP_CHAIN_DESC swapChainDesc = {
                .BufferDesc.Width = 0,
                .BufferDesc.Height = 0,
                .BufferDesc.RefreshRate.Numerator = 0,
                .BufferDesc.RefreshRate.Denominator = 0,
                .BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM,
                .BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED,
                .BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED,

                .SampleDesc.Count = 1,
                .SampleDesc.Quality = 0,

                .BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT,
                .BufferCount = 2,

                .OutputWindow = hwnd,
                .Windowed = TRUE,
                .SwapEffect = DXGI_SWAP_EFFECT_DISCARD,
                .Flags = 0
            };
            ASSERT(SUCCEEDED(D3D11CreateDeviceAndSwapChain(
                0,
                D3D_DRIVER_TYPE_HARDWARE,
                0,
                creationFlags,
                featureLevels,
                ARRAYSIZE(featureLevels),
                D3D11_SDK_VERSION,
                &swapChainDesc,
                &swapChain,
                &device,
                0,
                &deviceContext
            )));

            {
                IDXGIDevice* dxgiDevice;
                device->lpVtbl->QueryInterface(device,&IID_IDXGIDevice,&dxgiDevice);
                IDXGIAdapter* dxgiAdapter;
                dxgiDevice->lpVtbl->GetAdapter(dxgiDevice,&dxgiAdapter);
                IDXGIFactory* factory;
                dxgiAdapter->lpVtbl->GetParent(dxgiAdapter,&IID_IDXGIFactory,&factory);
                // disable silly Alt+Enter changing monitor resolution to match window size
                factory->lpVtbl->MakeWindowAssociation(factory,hwnd,DXGI_MWA_NO_ALT_ENTER);
                factory->lpVtbl->Release(factory);
                dxgiAdapter->lpVtbl->Release(dxgiAdapter);
                dxgiDevice->lpVtbl->Release(dxgiDevice);
            }

            {
                D3D11_TEXTURE2D_DESC desc ={
                    .Width = screen->width,
                    .Height = screen->height,
                    .MipLevels = 1,
                    .ArraySize = 1,
                    .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
                    .SampleDesc = { 1, 0 },
                    .Usage = D3D11_USAGE_DEFAULT,
                    .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
                    .BindFlags = D3D11_BIND_SHADER_RESOURCE,
                };
                device->lpVtbl->CreateTexture2D(device,&desc,0,&texture);
                device->lpVtbl->CreateShaderResourceView(device,(ID3D11Resource *)texture,0,&textureView);
            }

            {
                D3D11_SAMPLER_DESC desc =
                {
                    .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
                    .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
                    .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
                    .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
                };

                device->lpVtbl->CreateSamplerState(device,&desc,&sampler);
            }

            {
                // enable alpha blending
                D3D11_BLEND_DESC desc =
                {
                    .RenderTarget[0] =
                    {
                        .BlendEnable = 1,
                        .SrcBlend = D3D11_BLEND_SRC_ALPHA,
                        .DestBlend = D3D11_BLEND_INV_SRC_ALPHA,
                        .BlendOp = D3D11_BLEND_OP_ADD,
                        .SrcBlendAlpha = D3D11_BLEND_SRC_ALPHA,
                        .DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA,
                        .BlendOpAlpha = D3D11_BLEND_OP_ADD,
                        .RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL,
                },
                };
                device->lpVtbl->CreateBlendState(device,&desc,&blendState);
            }

            {
                // disable culling
                D3D11_RASTERIZER_DESC desc =
                {
                    .FillMode = D3D11_FILL_SOLID,
                    .CullMode = D3D11_CULL_NONE,
                };
                device->lpVtbl->CreateRasterizerState(device,&desc,&rasterizerState);
            }

            {
                // disable depth & stencil test
                D3D11_DEPTH_STENCIL_DESC desc =
                {
                    .DepthEnable = FALSE,
                    .DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL,
                    .DepthFunc = D3D11_COMPARISON_LESS,
                    .StencilEnable = FALSE,
                    .StencilReadMask = D3D11_DEFAULT_STENCIL_READ_MASK,
                    .StencilWriteMask = D3D11_DEFAULT_STENCIL_WRITE_MASK,
                    // .FrontFace = ... 
                    // .BackFace = ...
                };
                device->lpVtbl->CreateDepthStencilState(device,&desc,&depthStencilState);
            }

            CreateRenderTargets();

            {
                // these must match vertex shader input layout (VS_INPUT in vertex shader source below)
                D3D11_INPUT_ELEMENT_DESC desc[] =
                {
                    {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
                    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, offsetof(Vertex, texcoord), D3D11_INPUT_PER_VERTEX_DATA, 0},
                };

        #define STR2(x) #x
        #define STR(x) STR2(x)
                const char hlsl[] =
                    "#line " STR(__LINE__) "                                  \n\n" // actual line number in this file for nicer error messages
                    "                                                           \n"
                    "struct VS_INPUT                                            \n"
                    "{                                                          \n"
                    "     float2 pos   : POSITION;                              \n" // these names must match D3D11_INPUT_ELEMENT_DESC array
                    "     float2 uv    : TEXCOORD;                              \n"
                    "};                                                         \n"
                    "                                                           \n"
                    "struct PS_INPUT                                            \n"
                    "{                                                          \n"
                    "  float4 pos   : SV_POSITION;                              \n" // these names do not matter, except SV_... ones
                    "  float2 uv    : TEXCOORD;                                 \n"
                    "};                                                         \n"
                    "                                                           \n"
                    "sampler sampler0 : register(s0);                           \n" // s0 = sampler bound to slot 0
                    "                                                           \n"
                    "Texture2D<float4> texture0 : register(t0);                 \n" // t0 = shader resource bound to slot 0
                    "                                                           \n"
                    "PS_INPUT vs(VS_INPUT input)                                \n"
                    "{                                                          \n"
                    "    PS_INPUT output;                                       \n"
                    "    output.pos = float4(input.pos, 0, 1);                  \n"
                    "    output.uv = input.uv;                                  \n"
                    "    return output;                                         \n"
                    "}                                                          \n"
                    "                                                           \n"
                    "float4 ps(PS_INPUT input) : SV_TARGET                      \n"
                    "{                                                          \n"
                    "    return texture0.Sample(sampler0, input.uv);            \n"
                    "}                                                          \n";
                ;

                UINT flags = D3DCOMPILE_PACK_MATRIX_COLUMN_MAJOR | D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS;
        #if _DEBUG
                flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
        #else
                flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
        #endif

                ID3DBlob* error;

                ID3DBlob* vblob;
                HRESULT hr = D3DCompile(hlsl, sizeof(hlsl), 0, 0, 0, "vs", "vs_5_0", flags, 0, &vblob, &error);
                if (FAILED(hr))
                {
                    char *message = error->lpVtbl->GetBufferPointer(error);
                    fatal_error(message);
                }

                ID3DBlob* pblob;
                hr = D3DCompile(hlsl, sizeof(hlsl), 0, 0, 0, "ps", "ps_5_0", flags, 0, &pblob, &error);
                if (FAILED(hr))
                {
                    char *message = error->lpVtbl->GetBufferPointer(error);
                    fatal_error(message);
                }

                device->lpVtbl->CreateVertexShader(device,vblob->lpVtbl->GetBufferPointer(vblob),vblob->lpVtbl->GetBufferSize(vblob),0,&vshader);
                device->lpVtbl->CreatePixelShader(device,pblob->lpVtbl->GetBufferPointer(pblob),pblob->lpVtbl->GetBufferSize(pblob),0,&pshader);
                device->lpVtbl->CreateInputLayout(device,desc,ARRAYSIZE(desc),vblob->lpVtbl->GetBufferPointer(vblob),vblob->lpVtbl->GetBufferSize(vblob),&layout);

                pblob->lpVtbl->Release(pblob);
                vblob->lpVtbl->Release(vblob);
            }

            //init audio:
            {
                safe_coinit();
                ASSERT(SUCCEEDED(CoCreateInstance(&_CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &_IID_IMMDeviceEnumerator, (void**)&enu)));
                ASSERT(SUCCEEDED(enu->lpVtbl->GetDefaultAudioEndpoint(enu, eRender, eConsole, &dev)));
                ASSERT(SUCCEEDED(dev->lpVtbl->Activate(dev, &_IID_IAudioClient, CLSCTX_ALL, NULL, (void**)&client)));
                WAVEFORMATEXTENSIBLE fmtex = {0};
                fmtex.Format.nChannels = 2;
                fmtex.Format.nSamplesPerSec = TINY3D_SAMPLE_RATE;
                fmtex.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
                fmtex.Format.wBitsPerSample = 16;
                fmtex.Format.nBlockAlign = (fmtex.Format.nChannels * fmtex.Format.wBitsPerSample) / 8;
                fmtex.Format.nAvgBytesPerSec = fmtex.Format.nSamplesPerSec * fmtex.Format.nBlockAlign;
                fmtex.Format.cbSize = 22;   /* WORD + DWORD + GUID */
                fmtex.Samples.wValidBitsPerSample = 16;
                fmtex.dwChannelMask = SPEAKER_FRONT_LEFT|SPEAKER_FRONT_RIGHT;
                fmtex.SubFormat = _KSDATAFORMAT_SUBTYPE_PCM;
                REFERENCE_TIME dur = (REFERENCE_TIME)(((double)TINY3D_AUDIO_BUFSZ) / (((double)fmtex.Format.nSamplesPerSec) * (1.0/10000000.0)));
                ASSERT(SUCCEEDED(client->lpVtbl->Initialize(
                    client,
                    AUDCLNT_SHAREMODE_SHARED,
                    AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM|AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                    dur, 0, (WAVEFORMATEX*)&fmtex, 0)));
                ASSERT(SUCCEEDED(client->lpVtbl->GetService(client, &_IID_IAudioRenderClient, (void**)&renderClient)));
                ASSERT(SUCCEEDED(client->lpVtbl->Start(client)));
            }

            //register raw mouse input
            #define HID_USAGE_PAGE_GENERIC ((unsigned short) 0x01)
            #define HID_USAGE_GENERIC_MOUSE ((unsigned short) 0x02)
            RAWINPUTDEVICE rid = {
                .usUsagePage = HID_USAGE_PAGE_GENERIC,
                .usUsage = HID_USAGE_GENERIC_MOUSE,
                .dwFlags = RIDEV_INPUTSINK,
                .hwndTarget = hwnd
            };
            RegisterRawInputDevices(&rid, 1, sizeof(rid));
            break;
        }
        case WM_PAINT:{
            RECT cr = {0};
            GetClientRect(hwnd,&cr);
            int width = cr.right-cr.left;
            int height = cr.bottom-cr.top;

            static LARGE_INTEGER freq,tstart,t0,t1;
            static bool started = false;
            if (!started){
                QueryPerformanceFrequency(&freq);
                QueryPerformanceCounter(&tstart);
                t0 = tstart;
                t1 = tstart;
                started = true;
            } else {
                QueryPerformanceCounter(&t1);
            }

            UINT32 total;
            UINT32 padding;
            ASSERT(SUCCEEDED(client->lpVtbl->GetBufferSize(client, &total)));
            ASSERT(SUCCEEDED(client->lpVtbl->GetCurrentPadding(client, &padding)));
            UINT32 remaining = total - padding;
            int16_t *samples;
            ASSERT(SUCCEEDED(renderClient->lpVtbl->GetBuffer(renderClient, remaining, (BYTE **)&samples)));
            update((double)(t1.QuadPart-tstart.QuadPart) / (double)freq.QuadPart, (double)(t1.QuadPart-t0.QuadPart) / (double)freq.QuadPart, (int)remaining, samples);
            ASSERT(SUCCEEDED(renderClient->lpVtbl->ReleaseBuffer(renderClient,remaining,0)));
            t0 = t1;

            deviceContext->lpVtbl->OMSetRenderTargets(deviceContext, 1, &renderTargetView, depthStencilView);
            FLOAT clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            deviceContext->lpVtbl->ClearRenderTargetView(deviceContext, renderTargetView, clearColor);
            deviceContext->lpVtbl->ClearDepthStencilView(deviceContext, depthStencilView, D3D11_CLEAR_DEPTH, 1.0f, 0);

            {
                ID3D11Buffer* vbuffer;
                Vertex data[] =
                {
                    { {-1.0f, 1.0f }, { 0.0f, 1.0f } },
                    { {-1.0f, -1.0f }, {  0.0f,  0.0f }},
                    { { 1.0f, -1.0f }, { 1.0f,  0.0f }},

                    { {1.0f, -1.0f}, { 1.0f, 0.0f }},
                    { {1.0f, 1.0f}, {  1.0f,  1.0f }},
                    { {-1.0f, 1.0f}, { 0.0f,  1.0f }},
                };

                D3D11_BUFFER_DESC desc =
                {
                    .ByteWidth = sizeof(data),
                    .Usage = D3D11_USAGE_IMMUTABLE,
                    .BindFlags = D3D11_BIND_VERTEX_BUFFER,
                };

                D3D11_SUBRESOURCE_DATA initial = { .pSysMem = data };
                device->lpVtbl->CreateBuffer(device,&desc,&initial,&vbuffer);

                deviceContext->lpVtbl->IASetInputLayout(deviceContext,layout);
                deviceContext->lpVtbl->IASetPrimitiveTopology(deviceContext,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                UINT stride = sizeof(Vertex);
                UINT offset = 0;
                deviceContext->lpVtbl->IASetVertexBuffers(deviceContext,0,1,&vbuffer,&stride,&offset);

                deviceContext->lpVtbl->VSSetShader(deviceContext,vshader,0,0);

                RECT cr;
                GetClientRect(hwnd,&cr);
                int cwidth = cr.right-cr.left;
                int cheight = cr.bottom-cr.top;
                int scale = 1;
                while (screen->width*scale <= cwidth && screen->height*scale <= cheight){
                    scale++;
                }
                scale--;
                int scaledWidth = scale * screen->width;
                int scaledHeight = scale * screen->height;
                D3D11_VIEWPORT viewport = {
                    .TopLeftX = (FLOAT)(cwidth/2-scaledWidth/2),
                    .TopLeftY = (FLOAT)(cheight/2-scaledHeight/2),
                    .Width = (FLOAT)scaledWidth,
                    .Height = (FLOAT)scaledHeight,
                    .MinDepth = 0,
                    .MaxDepth = 1,
                };
                deviceContext->lpVtbl->RSSetViewports(deviceContext,1,&viewport);
                deviceContext->lpVtbl->RSSetState(deviceContext,rasterizerState);

                deviceContext->lpVtbl->PSSetSamplers(deviceContext,0,1,&sampler);
                deviceContext->lpVtbl->UpdateSubresource(deviceContext,(ID3D11Resource *)texture,0,0,screen->pixels,screen->width*sizeof(*screen->pixels),0);
                deviceContext->lpVtbl->PSSetShaderResources(deviceContext,0,1,&textureView);
                deviceContext->lpVtbl->PSSetShader(deviceContext,pshader,0,0);

                //deviceContext->lpVtbl->OMSetBlendState(deviceContext,blendState,0,~0U);
                deviceContext->lpVtbl->OMSetDepthStencilState(deviceContext,depthStencilState,0);

                deviceContext->lpVtbl->Draw(deviceContext,6,0);

                vbuffer->lpVtbl->Release(vbuffer); //TODO: find out if this is shit
            }

            swapChain->lpVtbl->Present(swapChain, 1, 0);

            return 0;
        }
        case WM_SIZE:{
            deviceContext->lpVtbl->OMSetRenderTargets(deviceContext,0,0,0);
            renderTargetView->lpVtbl->Release(renderTargetView);
            depthStencilView->lpVtbl->Release(depthStencilView);
            ASSERT(SUCCEEDED(swapChain->lpVtbl->ResizeBuffers(swapChain,0,0,0,DXGI_FORMAT_UNKNOWN,0)));
            CreateRenderTargets();
            break;
        }
        case WM_DESTROY:{
            PostQuitMessage(0);
            break;
        }
        case WM_MOUSEMOVE:{
            if (!mouse_is_locked){
                mousemove(GET_X_LPARAM(lparam),GET_Y_LPARAM(lparam));
            }
            return 0;
        }
        case WM_INPUT:{
            UINT size = sizeof(RAWINPUT);
			static RAWINPUT raw[sizeof(RAWINPUT)];
			GetRawInputData((HRAWINPUT)lparam, RID_INPUT, raw, &size, sizeof(RAWINPUTHEADER));
            if (raw->header.dwType == RIM_TYPEMOUSE){
                if (mouse_is_locked){
                    mousemove(raw->data.mouse.lLastX,raw->data.mouse.lLastY);
                }
                //cameraRotate(&cam, raw->data.mouse.lLastX,raw->data.mouse.lLastY, -0.002f);
                //if (raw->data.mouse.usButtonFlags & RI_MOUSE_WHEEL)
                //	input.mouse.wheel = (*(short*)&raw->data.mouse.usButtonData) / WHEEL_DELTA;
            }
		    return 0;
	    }
        case WM_LBUTTONDOWN:{
            keydown(KEY_MOUSE_LEFT);
            return 0;
        }
        case WM_RBUTTONDOWN:{
            keydown(KEY_MOUSE_RIGHT);
            return 0;
        }
        case WM_LBUTTONUP:{
            keyup(KEY_MOUSE_LEFT);
            return 0;
        }
        case WM_RBUTTONUP:{
            keyup(KEY_MOUSE_RIGHT);
            return 0;
        }
        case WM_KEYDOWN:{
            if (!(HIWORD(lparam) & KF_REPEAT)){
                keydown((lparam & 0xff0000)>>16);
            }
            break;
        }
        case WM_KEYUP:{
            keyup((lparam & 0xff0000)>>16);
            break;
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void open_window(image_t *image, int scale){
    WNDCLASSEXW wcex = {
        .cbSize = sizeof(wcex),
        .style = CS_HREDRAW | CS_VREDRAW,
        .lpfnWndProc = WndProc,
        .hInstance = GetModuleHandleW(0),
        //.hIcon = LoadIconW(GetModuleHandleW(0),MAKEINTRESOURCEW(RID_ICON)),
        .hCursor = LoadCursorW(0,IDC_ARROW),
        .lpszClassName = L"tiny3d",
        .hIconSm = 0,
    };
    ASSERT(RegisterClassExW(&wcex));

    screen = image;
    ASSERT(screen->width > 0 && screen->height > 0);
    RECT initialRect = {0, 0, screen->width*scale, screen->height*scale};
    AdjustWindowRect(&initialRect,WS_OVERLAPPEDWINDOW,FALSE);
    LONG initialWidth = initialRect.right - initialRect.left;
    LONG initialHeight = initialRect.bottom - initialRect.top;
    gwnd = CreateWindowExW(
        0, //WS_EX_OVERLAPPEDWINDOW fucks up the borders when maximized
        wcex.lpszClassName,
        wcex.lpszClassName,
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        GetSystemMetrics(SM_CXSCREEN)/2-initialWidth/2,
        GetSystemMetrics(SM_CYSCREEN)/2-initialHeight/2,
        initialWidth, 
        initialHeight,
        0, 0, wcex.hInstance, 0
    );
    ASSERT(gwnd);

    MSG msg;
    while (GetMessageW(&msg,0,0,0)){
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}