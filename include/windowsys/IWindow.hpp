#ifndef IWINDOW_HPP
#define IWINDOW_HPP

#include <string>

namespace boo
{
    
class IWindowCallback
{
public:
    enum EMouseButton
    {
        BUTTON_NONE      = 0,
        BUTTON_PRIMARY   = 1,
        BUTTON_SECONDARY = 2,
        BUTTON_MIDDLE    = 3,
        BUTTON_AUX1      = 4,
        BUTTON_AUX2      = 5
    };
    
    struct SWindowCoord
    {
        unsigned pixel[2];
        unsigned virtualPixel[2];
        float norm[2];
    };
    
    struct SScrollDelta
    {
        float delta[2];
        bool isFine; /* Use system-scale fine-scroll (for scrollable-trackpads) */
    };
    
    enum ESpecialKey
    {
        KEY_NONE       = 0,
        KEY_F1         = 1,
        KEY_F2         = 2,
        KEY_F3         = 3,
        KEY_F4         = 4,
        KEY_F5         = 5,
        KEY_F6         = 6,
        KEY_F7         = 7,
        KEY_F8         = 8,
        KEY_F9         = 9,
        KEY_F10        = 10,
        KEY_F11        = 11,
        KEY_F12        = 12,
        KEY_ESC        = 13,
        KEY_ENTER      = 14,
        KEY_BACKSPACE  = 15,
        KEY_INSERT     = 16,
        KEY_DELETE     = 17,
        KEY_HOME       = 18,
        KEY_END        = 19,
        KEY_PGUP       = 20,
        KEY_PGDOWN     = 21,
        KEY_LEFT       = 22,
        KEY_RIGHT      = 23,
        KEY_UP         = 24,
        KEY_DOWN       = 25
    };
    
    enum EModifierKey
    {
        MKEY_NONE    = 0,
        MKEY_CTRL    = 1<<0,
        MKEY_ALT     = 1<<2,
        MKEY_SHIFT   = 1<<3,
        MKEY_COMMAND = 1<<4
    };
    
    virtual void mouseDown(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {(void)coord;(void)button;(void)mods;}
    virtual void mouseUp(const SWindowCoord& coord, EMouseButton button, EModifierKey mods)
    {(void)coord;(void)button;(void)mods;}
    virtual void mouseMove(const SWindowCoord& coord)
    {(void)coord;}
    virtual void scroll(const SScrollDelta& scroll)
    {(void)scroll;};
    
    virtual void touchDown(const SWindowCoord& coord, uintptr_t tid)
    {(void)coord;(void)tid;}
    virtual void touchUp(const SWindowCoord& coord, uintptr_t tid)
    {(void)coord;(void)tid;}
    virtual void touchMove(const SWindowCoord& coord, uintptr_t tid)
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
    
};

class IWindow
{
public:
    
    virtual ~IWindow() {}
    
    virtual void setCallback(IWindowCallback* cb)=0;
    
    virtual void showWindow()=0;
    virtual void hideWindow()=0;
    
    virtual std::string getTitle()=0;
    virtual void setTitle(const std::string& title)=0;

    virtual void setWindowFrameDefault()=0;
    virtual void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const=0;
    virtual void setWindowFrame(float x, float y, float w, float h)=0;
    virtual float getVirtualPixelFactor() const=0;
    
    virtual bool isFullscreen() const=0;
    virtual void setFullscreen(bool fs)=0;
    
    enum ETouchType
    {
        TOUCH_NONE     = 0,
        TOUCH_DISPLAY  = 1,
        TOUCH_TRACKPAD = 2
    };
    virtual ETouchType getTouchType() const=0;
    
};
    
IWindow* IWindowNew();
    
}

#endif // IWINDOW_HPP
