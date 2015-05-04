#ifndef IWINDOW_HPP
#define IWINDOW_HPP

#include <string>

namespace boo
{

class IWindow
{
public:
    
    virtual ~IWindow() {}
    
    virtual void showWindow()=0;
    virtual void hideWindow()=0;
    
    virtual std::string getTitle()=0;
    virtual void setTitle(const std::string& title)=0;

    virtual void setWindowFrameDefault()=0;
    virtual void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const=0;
    virtual void setWindowFrame(float x, float y, float w, float h)=0;
    
    virtual bool isFullscreen() const=0;
    virtual void setFullscreen(bool fs)=0;
    
};
    
IWindow* IWindowNew();
    
}

#endif // IWINDOW_HPP
