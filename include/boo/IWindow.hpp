#ifndef IWINDOW_HPP
#define IWINDOW_HPP

#include "System.hpp"
#include <memory>
#include <algorithm>
#include <cstring>

#undef min
#undef max

namespace boo
{
struct IGraphicsCommandQueue;
struct IGraphicsDataFactory;
struct IAudioVoiceEngine;

enum class EMouseButton
{
    None      = 0,
    Primary   = 1,
    Secondary = 2,
    Middle    = 3,
    Aux1      = 4,
    Aux2      = 5
};

struct SWindowCoord
{
    int pixel[2];
    int virtualPixel[2];
    float norm[2];
};

struct SWindowRect
{
    int location[2];
    int size[2];

    SWindowRect() {std::memset(this, 0, sizeof(SWindowRect));}

    SWindowRect(int x, int y, int w, int h)
    {
        location[0] = x;
        location[1] = y;
        size[0] = w;
        size[1] = h;
    }

    bool operator!=(const SWindowRect& other) const
    {
        return location[0] != other.location[0] ||
               location[1] != other.location[1] ||
               size[0] != other.size[0] ||
               size[1] != other.size[1];
    }
    bool operator==(const SWindowRect& other) const {return !(*this != other);}

    bool coordInRect(const SWindowCoord& coord) const
    {
        return coord.pixel[0] >= location[0] && coord.pixel[0] < location[0] + size[0] &&
               coord.pixel[1] >= location[1] && coord.pixel[1] < location[1] + size[1];
    }

    SWindowRect intersect(const SWindowRect& other) const
    {
        if (location[0] < other.location[0] + other.size[0] &&
            location[0] + size[0] > other.location[0] &&
            location[1] < other.location[1] + other.size[1] &&
            location[1] + size[1] > other.location[1])
        {
            SWindowRect ret;
            ret.location[0] = std::max(location[0], other.location[0]);
            ret.location[1] = std::max(location[1], other.location[1]);
            ret.size[0] = std::min(location[0] + size[0], other.location[0] + other.size[0]) - ret.location[0];
            ret.size[1] = std::min(location[1] + size[1], other.location[1] + other.size[1]) - ret.location[1];
            return ret;
        }
        return {};
    }
};

struct STouchCoord
{
    double coord[2];
};

struct SScrollDelta
{
    double delta[2];
    bool isFine; /* Use system-scale fine-scroll (for scrollable-trackpads) */
    bool isAccelerated = false; /* System performs acceleration computation */

    SScrollDelta operator+(const SScrollDelta& other)
    {
        SScrollDelta ret;
        ret.delta[0] = delta[0] + other.delta[0];
        ret.delta[1] = delta[1] + other.delta[1];
        ret.isFine = isFine || other.isFine;
        ret.isAccelerated = isAccelerated || other.isAccelerated;
        return ret;
    }
    SScrollDelta& operator+=(const SScrollDelta& other)
    {
        delta[0] += other.delta[0];
        delta[1] += other.delta[1];
        isFine |= other.isFine;
        isAccelerated |= other.isAccelerated;
        return *this;
    }
    void zeroOut() {delta[0] = 0.0; delta[1] = 0.0;}
};

enum class ESpecialKey
{
    None       = 0,
    F1         = 1,
    F2         = 2,
    F3         = 3,
    F4         = 4,
    F5         = 5,
    F6         = 6,
    F7         = 7,
    F8         = 8,
    F9         = 9,
    F10        = 10,
    F11        = 11,
    F12        = 12,
    Esc        = 13,
    Enter      = 14,
    Backspace  = 15,
    Insert     = 16,
    Delete     = 17,
    Home       = 18,
    End        = 19,
    PgUp       = 20,
    PgDown     = 21,
    Left       = 22,
    Right      = 23,
    Up         = 24,
    Down       = 25
};

enum class EModifierKey
{
    None    = 0,
    Ctrl    = 1<<0,
    Alt     = 1<<2,
    Shift   = 1<<3,
    Command = 1<<4,
    CtrlCommand = EModifierKey::Ctrl | EModifierKey::Command
};
ENABLE_BITWISE_ENUM(EModifierKey)
    
struct ITextInputCallback
{
    virtual bool hasMarkedText() const=0;
    virtual std::pair<int,int> markedRange() const=0;
    virtual std::pair<int,int> selectedRange() const=0;
    virtual void setMarkedText(const std::string& str,
                               const std::pair<int,int>& selectedRange,
                               const std::pair<int,int>& replacementRange)=0;
    virtual void unmarkText()=0;
    
    virtual std::string substringForRange(const std::pair<int,int>& range,
                                          std::pair<int,int>& actualRange) const=0;
    virtual void insertText(const std::string& str, const std::pair<int,int>& range={-1,0})=0;
    virtual int characterIndexAtPoint(const SWindowCoord& point) const=0;
    virtual SWindowRect rectForCharacterRange(const std::pair<int,int>& range,
                                              std::pair<int,int>& actualRange) const=0;
};

class IWindowCallback
{
public:
    virtual void resized(const SWindowRect& rect)
    {(void)rect;}
    virtual void mouseDown(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {(void)coord;(void)button;(void)mods;}
    virtual void mouseUp(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {(void)coord;(void)button;(void)mods;}
    virtual void mouseMove(const SWindowCoord& coord)
    {(void)coord;}
    virtual void mouseEnter(const SWindowCoord& coord)
    {(void)coord;}
    virtual void mouseLeave(const SWindowCoord& coord)
    {(void)coord;}
    virtual void scroll(const SWindowCoord& coord, const SScrollDelta& scroll)
    {(void)coord;(void)scroll;}

    virtual void touchDown(const STouchCoord& coord, uintptr_t tid)
    {(void)coord;(void)tid;}
    virtual void touchUp(const STouchCoord& coord, uintptr_t tid)
    {(void)coord;(void)tid;}
    virtual void touchMove(const STouchCoord& coord, uintptr_t tid)
    {(void)coord;(void)tid;}

    virtual void charKeyDown(unsigned long charCode, EModifierKey mods, bool isRepeat)
    {(void)charCode;(void)mods;(void)isRepeat;}
    virtual void charKeyUp(unsigned long charCode, EModifierKey mods)
    {(void)charCode;(void)mods;}
    virtual void specialKeyDown(ESpecialKey key, EModifierKey mods, bool isRepeat)
    {(void)key;(void)mods;(void)isRepeat;}
    virtual void specialKeyUp(ESpecialKey key, EModifierKey mods)
    {(void)key;(void)mods;}
    virtual void modKeyDown(EModifierKey mod, bool isRepeat)
    {(void)mod;(void)isRepeat;}
    virtual void modKeyUp(EModifierKey mod) {(void)mod;}
    
    virtual ITextInputCallback* getTextInputCallback() {return nullptr;}
    
    virtual void focusLost() {}
    virtual void focusGained() {}
    virtual void windowMoved(const SWindowRect& rect)
    { (void)rect; }

    virtual void destroyed()
    {}
};

enum class ETouchType
{
    None     = 0,
    Display  = 1,
    Trackpad = 2
};

enum class EWindowStyle
{
    None     = 0,
    Titlebar = 1<<0,
    Resize   = 1<<1,
    Close    = 1<<2,

    Default = Titlebar | Resize | Close
};
ENABLE_BITWISE_ENUM(EWindowStyle)

enum class EMouseCursor
{
    None            = 0,
    Pointer         = 1,
    HorizontalArrow = 2,
    VerticalArrow   = 3,
    IBeam           = 4,
    Crosshairs      = 5
};

enum class EClipboardType
{
    None       = 0,
    String     = 1,
    UTF8String = 2,
    PNGImage   = 3
};

class IWindow
{
public:
    
    virtual ~IWindow() {}
    
    virtual void setCallback(IWindowCallback* cb)=0;
    
    virtual void showWindow()=0;
    virtual void hideWindow()=0;
    
    virtual SystemString getTitle()=0;
    virtual void setTitle(const SystemString& title)=0;

    virtual void setCursor(EMouseCursor cursor)=0;
    virtual void setWaitCursor(bool wait)=0;

    virtual void setWindowFrameDefault()=0;
    virtual void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const=0;
    virtual void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const=0;
    virtual SWindowRect getWindowFrame() const
    {
        SWindowRect retval;
        getWindowFrame(retval.location[0], retval.location[1], retval.size[0], retval.size[1]);
        return retval;
    }
    virtual void setWindowFrame(float x, float y, float w, float h)=0;
    virtual void setWindowFrame(int x, int y, int w, int h)=0;
    virtual void setWindowFrame(const SWindowRect& rect)
    {
        setWindowFrame(rect.location[0], rect.location[1], rect.size[0], rect.size[1]);
    }
    virtual float getVirtualPixelFactor() const=0;
    
    virtual bool isFullscreen() const=0;
    virtual void setFullscreen(bool fs)=0;

    virtual void claimKeyboardFocus(const int coord[2])=0;
    virtual bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz)=0;
    virtual std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz)=0;

    virtual void waitForRetrace(IAudioVoiceEngine* voxEngine=nullptr)=0;
    
    virtual uintptr_t getPlatformHandle() const=0;
    virtual void _incomingEvent(void* event) {(void)event;}

    virtual ETouchType getTouchType() const=0;

    virtual void setStyle(EWindowStyle style)=0;
    virtual EWindowStyle getStyle() const=0;

    virtual void setTouchBarProvider(void*) {}

    virtual IGraphicsCommandQueue* getCommandQueue()=0;
    virtual IGraphicsDataFactory* getDataFactory()=0;

    /* Creates a new context on current thread!! Call from main client thread */
    virtual IGraphicsDataFactory* getMainContextDataFactory()=0;

    /* Creates a new context on current thread!! Call from client loading thread */
    virtual IGraphicsDataFactory* getLoadContextDataFactory()=0;
};
    
}

#endif // IWINDOW_HPP
