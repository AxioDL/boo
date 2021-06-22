#pragma once

#include <algorithm>
#include <array>
#include <memory>

#include "boo/System.hpp"

#undef min
#undef max

namespace boo {
struct IGraphicsCommandQueue;
struct IGraphicsDataFactory;
struct IAudioVoiceEngine;

enum class EMouseButton { None = 0, Primary = 1, Secondary = 2, Middle = 3, Aux1 = 4, Aux2 = 5 };

struct SWindowCoord {
  std::array<int, 2> pixel;
  std::array<int, 2> virtualPixel;
  std::array<float, 2> norm;
};

struct SWindowRect {
  std::array<int, 2> location{};
  std::array<int, 2> size{};

  constexpr SWindowRect() noexcept = default;
  constexpr SWindowRect(int x, int y, int w, int h) noexcept : location{x, y}, size{w, h} {}

  constexpr bool operator==(const SWindowRect& other) const noexcept {
    return location[0] == other.location[0] && location[1] == other.location[1] && size[0] == other.size[0] &&
           size[1] == other.size[1];
  }
  constexpr bool operator!=(const SWindowRect& other) const noexcept { return !operator==(other); }

  constexpr bool coordInRect(const SWindowCoord& coord) const noexcept {
    return coord.pixel[0] >= location[0] && coord.pixel[0] < location[0] + size[0] && coord.pixel[1] >= location[1] &&
           coord.pixel[1] < location[1] + size[1];
  }

  constexpr SWindowRect intersect(const SWindowRect& other) const noexcept {
    if (location[0] < other.location[0] + other.size[0] && location[0] + size[0] > other.location[0] &&
        location[1] < other.location[1] + other.size[1] && location[1] + size[1] > other.location[1]) {
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

struct STouchCoord {
  std::array<double, 2> coord;
};

struct SScrollDelta {
  std::array<double, 2> delta{};
  bool isFine = false;        /* Use system-scale fine-scroll (for scrollable-trackpads) */
  bool isAccelerated = false; /* System performs acceleration computation */

  constexpr SScrollDelta operator+(const SScrollDelta& other) const noexcept {
    SScrollDelta ret;
    ret.delta[0] = delta[0] + other.delta[0];
    ret.delta[1] = delta[1] + other.delta[1];
    ret.isFine = isFine || other.isFine;
    ret.isAccelerated = isAccelerated || other.isAccelerated;
    return ret;
  }
  constexpr SScrollDelta operator-(const SScrollDelta& other) const noexcept {
    SScrollDelta ret;
    ret.delta[0] = delta[0] - other.delta[0];
    ret.delta[1] = delta[1] - other.delta[1];
    ret.isFine = isFine || other.isFine;
    ret.isAccelerated = isAccelerated || other.isAccelerated;
    return ret;
  }
  constexpr SScrollDelta& operator+=(const SScrollDelta& other) noexcept {
    delta[0] += other.delta[0];
    delta[1] += other.delta[1];
    isFine |= other.isFine;
    isAccelerated |= other.isAccelerated;
    return *this;
  }
  constexpr void zeroOut() noexcept { delta = {}; }
  constexpr bool isZero() const noexcept { return delta[0] == 0.0 && delta[1] == 0.0; }
};

enum class ESpecialKey {
  None = 0,
  F1 = 1,
  F2 = 2,
  F3 = 3,
  F4 = 4,
  F5 = 5,
  F6 = 6,
  F7 = 7,
  F8 = 8,
  F9 = 9,
  F10 = 10,
  F11 = 11,
  F12 = 12,
  Esc = 13,
  Enter = 14,
  Backspace = 15,
  Insert = 16,
  Delete = 17,
  Home = 18,
  End = 19,
  PgUp = 20,
  PgDown = 21,
  Left = 22,
  Right = 23,
  Up = 24,
  Down = 25,
  Tab = 26,
  MAX = 27,
};

enum class EModifierKey {
  None = 0,
  Ctrl = 1 << 0,
  Alt = 1 << 2,
  Shift = 1 << 3,
  Command = 1 << 4,
  CtrlCommand = EModifierKey::Ctrl | EModifierKey::Command
};
ENABLE_BITWISE_ENUM(EModifierKey)

struct ITextInputCallback {
  virtual bool hasMarkedText() const = 0;
  virtual std::pair<int, int> markedRange() const = 0;
  virtual std::pair<int, int> selectedRange() const = 0;
  virtual void setMarkedText(std::string_view str, const std::pair<int, int>& selectedRange,
                             const std::pair<int, int>& replacementRange) = 0;
  virtual void unmarkText() = 0;

  virtual std::string substringForRange(const std::pair<int, int>& range, std::pair<int, int>& actualRange) const = 0;
  virtual void insertText(std::string_view str, const std::pair<int, int>& range = {-1, 0}) = 0;
  virtual int characterIndexAtPoint(const SWindowCoord& point) const = 0;
  virtual SWindowRect rectForCharacterRange(const std::pair<int, int>& range,
                                            std::pair<int, int>& actualRange) const = 0;
};

class IWindowCallback {
public:
  virtual void resized([[maybe_unused]] const SWindowRect& rect, [[maybe_unused]] bool sync) {}
  virtual void mouseDown([[maybe_unused]] const SWindowCoord& coord, [[maybe_unused]] EMouseButton button,
                         [[maybe_unused]] EModifierKey mods) {}
  virtual void mouseUp([[maybe_unused]] const SWindowCoord& coord, [[maybe_unused]] EMouseButton button,
                       [[maybe_unused]] EModifierKey mods) {}
  virtual void mouseMove([[maybe_unused]] const SWindowCoord& coord) {}
  virtual void mouseEnter([[maybe_unused]] const SWindowCoord& coord) {}
  virtual void mouseLeave([[maybe_unused]] const SWindowCoord& coord) {}
  virtual void scroll([[maybe_unused]] const SWindowCoord& coord, [[maybe_unused]] const SScrollDelta& scroll) {}

  virtual void touchDown([[maybe_unused]] const STouchCoord& coord, [[maybe_unused]] uintptr_t tid) {}
  virtual void touchUp([[maybe_unused]] const STouchCoord& coord, [[maybe_unused]] uintptr_t tid) {}
  virtual void touchMove([[maybe_unused]] const STouchCoord& coord, [[maybe_unused]] uintptr_t tid) {}

  virtual void charKeyDown([[maybe_unused]] unsigned long charCode, [[maybe_unused]] EModifierKey mods,
                           [[maybe_unused]] bool isRepeat) {}
  virtual void charKeyUp([[maybe_unused]] unsigned long charCode, [[maybe_unused]] EModifierKey mods) {}
  virtual void specialKeyDown([[maybe_unused]] ESpecialKey key, [[maybe_unused]] EModifierKey mods,
                              [[maybe_unused]] bool isRepeat) {}
  virtual void specialKeyUp([[maybe_unused]] ESpecialKey key, [[maybe_unused]] EModifierKey mods) {}
  virtual void modKeyDown([[maybe_unused]] EModifierKey mod, [[maybe_unused]] bool isRepeat) {}
  virtual void modKeyUp([[maybe_unused]] EModifierKey mod) {}

  virtual ITextInputCallback* getTextInputCallback() { return nullptr; }

  virtual void focusLost() {}
  virtual void focusGained() {}
  virtual void windowMoved([[maybe_unused]] const SWindowRect& rect) {}

  virtual void destroyed() {}
};

enum class ETouchType { None = 0, Display = 1, Trackpad = 2 };

enum class EWindowStyle {
  None = 0,
  Titlebar = 1 << 0,
  Resize = 1 << 1,
  Close = 1 << 2,

  Default = Titlebar | Resize | Close
};
ENABLE_BITWISE_ENUM(EWindowStyle)

enum class EMouseCursor {
  None = 0,
  Pointer = 1,
  HorizontalArrow = 2,
  VerticalArrow = 3,
  IBeam = 4,
  Crosshairs = 5,
  BottomRightArrow = 6,
  BottomLeftArrow = 7,
  Hand = 8,
  NotAllowed = 9,
};

enum class EClipboardType { None = 0, String = 1, UTF8String = 2, PNGImage = 3 };

class IWindow : public std::enable_shared_from_this<IWindow> {
public:
  virtual ~IWindow() = default;

  virtual void setCallback(IWindowCallback* cb) = 0;

  virtual void closeWindow() = 0;
  virtual void showWindow() = 0;
  virtual void hideWindow() = 0;

  virtual std::string getTitle() = 0;
  virtual void setTitle(std::string_view title) = 0;

  virtual void setCursor(EMouseCursor cursor) = 0;
  virtual void setWaitCursor(bool wait) = 0;

  virtual double getWindowRefreshRate() const = 0;
  virtual void setWindowFrameDefault() = 0;
  virtual void getWindowFrame(float& xOut, float& yOut, float& wOut, float& hOut) const = 0;
  virtual void getWindowFrame(int& xOut, int& yOut, int& wOut, int& hOut) const = 0;
  virtual SWindowRect getWindowFrame() const {
    SWindowRect retval;
    getWindowFrame(retval.location[0], retval.location[1], retval.size[0], retval.size[1]);
    return retval;
  }
  virtual void setWindowFrame(float x, float y, float w, float h) = 0;
  virtual void setWindowFrame(int x, int y, int w, int h) = 0;
  virtual void setWindowFrame(const SWindowRect& rect) {
    setWindowFrame(rect.location[0], rect.location[1], rect.size[0], rect.size[1]);
  }
  virtual float getVirtualPixelFactor() const = 0;

  virtual bool isFullscreen() const = 0;
  virtual void setFullscreen(bool fs) = 0;

  virtual void claimKeyboardFocus(const int coord[2]) = 0;
  virtual bool clipboardCopy(EClipboardType type, const uint8_t* data, size_t sz) = 0;
  virtual std::unique_ptr<uint8_t[]> clipboardPaste(EClipboardType type, size_t& sz) = 0;

  virtual int waitForRetrace() = 0;

  virtual uintptr_t getPlatformHandle() const = 0;
  virtual bool _incomingEvent([[maybe_unused]] void* event) { return false; }
  virtual void _cleanup() {}

  virtual ETouchType getTouchType() const = 0;

  virtual void setStyle(EWindowStyle style) = 0;
  virtual EWindowStyle getStyle() const = 0;

  virtual void setTouchBarProvider([[maybe_unused]] void* provider) {}

  virtual IGraphicsCommandQueue* getCommandQueue() = 0;
  virtual IGraphicsDataFactory* getDataFactory() = 0;

  /* Creates a new context on current thread!! Call from main client thread */
  virtual IGraphicsDataFactory* getMainContextDataFactory() = 0;

  /* Creates a new context on current thread!! Call from client loading thread */
  virtual IGraphicsDataFactory* getLoadContextDataFactory() = 0;
};

} // namespace boo
