#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <vector>

#include "boo/IWindow.hpp"
#include "nxstl/condition_variable"

namespace boo {

template <class Receiver>
struct DeferredWindowEvents : public IWindowCallback {
  Receiver& m_rec;
  std::mutex m_mt;
  std::condition_variable m_resizeCv;
  DeferredWindowEvents(Receiver& rec) : m_rec(rec) {}

  bool m_destroyed = false;
  void destroyed() override { m_destroyed = true; }

  bool m_hasResize = false;
  SWindowRect m_latestResize;
  void resized(const SWindowRect& rect, bool sync) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_latestResize = rect;
    m_hasResize = true;
    if (sync)
      m_resizeCv.wait_for(lk, std::chrono::milliseconds(500));
  }

  struct Command {
    enum class Type {
      MouseDown,
      MouseUp,
      MouseMove,
      MouseEnter,
      MouseLeave,
      Scroll,
      TouchDown,
      TouchUp,
      TouchMove,
      CharKeyDown,
      CharKeyUp,
      SpecialKeyDown,
      SpecialKeyUp,
      ModKeyDown,
      ModKeyUp
    } m_type;

    SWindowCoord m_coord;
    EMouseButton m_button;
    EModifierKey m_mods;
    SScrollDelta m_scroll;
    STouchCoord m_tCoord;
    uintptr_t m_tid;
    unsigned long m_charcode;
    ESpecialKey m_special;
    bool m_isRepeat;

    void dispatch(Receiver& rec) const {
      switch (m_type) {
      case Type::MouseDown:
        rec.mouseDown(m_coord, m_button, m_mods);
        break;
      case Type::MouseUp:
        rec.mouseUp(m_coord, m_button, m_mods);
        break;
      case Type::MouseMove:
        rec.mouseMove(m_coord);
        break;
      case Type::MouseEnter:
        rec.mouseEnter(m_coord);
        break;
      case Type::MouseLeave:
        rec.mouseLeave(m_coord);
        break;
      case Type::Scroll:
        rec.scroll(m_coord, m_scroll);
        break;
      case Type::TouchDown:
        rec.touchDown(m_tCoord, m_tid);
        break;
      case Type::TouchUp:
        rec.touchUp(m_tCoord, m_tid);
        break;
      case Type::TouchMove:
        rec.touchMove(m_tCoord, m_tid);
        break;
      case Type::CharKeyDown:
        rec.charKeyDown(m_charcode, m_mods, m_isRepeat);
        break;
      case Type::CharKeyUp:
        rec.charKeyUp(m_charcode, m_mods);
        break;
      case Type::SpecialKeyDown:
        rec.specialKeyDown(m_special, m_mods, m_isRepeat);
        break;
      case Type::SpecialKeyUp:
        rec.specialKeyUp(m_special, m_mods);
        break;
      case Type::ModKeyDown:
        rec.modKeyDown(m_mods, m_isRepeat);
        break;
      case Type::ModKeyUp:
        rec.modKeyUp(m_mods);
        break;
      default:
        break;
      }
    }

    Command(Type tp) : m_type(tp) {}
  };
  std::vector<Command> m_cmds;

  void mouseDown(const SWindowCoord& coord, EMouseButton button, EModifierKey mods) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::MouseDown);
    m_cmds.back().m_coord = coord;
    m_cmds.back().m_button = button;
    m_cmds.back().m_mods = mods;
  }

  void mouseUp(const SWindowCoord& coord, EMouseButton button, EModifierKey mods) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::MouseUp);
    m_cmds.back().m_coord = coord;
    m_cmds.back().m_button = button;
    m_cmds.back().m_mods = mods;
  }

  void mouseMove(const SWindowCoord& coord) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::MouseMove);
    m_cmds.back().m_coord = coord;
  }

  void mouseEnter(const SWindowCoord& coord) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::MouseEnter);
    m_cmds.back().m_coord = coord;
  }

  void mouseLeave(const SWindowCoord& coord) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::MouseLeave);
    m_cmds.back().m_coord = coord;
  }

  void scroll(const SWindowCoord& coord, const SScrollDelta& scroll) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::Scroll);
    m_cmds.back().m_coord = coord;
    m_cmds.back().m_scroll = scroll;
  }

  void touchDown(const STouchCoord& coord, uintptr_t tid) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::TouchDown);
    m_cmds.back().m_tCoord = coord;
    m_cmds.back().m_tid = tid;
  }

  void touchUp(const STouchCoord& coord, uintptr_t tid) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::TouchUp);
    m_cmds.back().m_tCoord = coord;
    m_cmds.back().m_tid = tid;
  }

  void touchMove(const STouchCoord& coord, uintptr_t tid) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::TouchMove);
    m_cmds.back().m_tCoord = coord;
    m_cmds.back().m_tid = tid;
  }

  void charKeyDown(unsigned long charCode, EModifierKey mods, bool isRepeat) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::CharKeyDown);
    m_cmds.back().m_charcode = charCode;
    m_cmds.back().m_mods = mods;
    m_cmds.back().m_isRepeat = isRepeat;
  }

  void charKeyUp(unsigned long charCode, EModifierKey mods) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::CharKeyUp);
    m_cmds.back().m_charcode = charCode;
    m_cmds.back().m_mods = mods;
  }

  void specialKeyDown(ESpecialKey key, EModifierKey mods, bool isRepeat) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::SpecialKeyDown);
    m_cmds.back().m_special = key;
    m_cmds.back().m_mods = mods;
    m_cmds.back().m_isRepeat = isRepeat;
  }

  void specialKeyUp(ESpecialKey key, EModifierKey mods) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::SpecialKeyUp);
    m_cmds.back().m_special = key;
    m_cmds.back().m_mods = mods;
  }

  void modKeyDown(EModifierKey mod, bool isRepeat) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::ModKeyDown);
    m_cmds.back().m_mods = mod;
    m_cmds.back().m_isRepeat = isRepeat;
  }

  void modKeyUp(EModifierKey mod) override {
    std::unique_lock<std::mutex> lk(m_mt);
    m_cmds.emplace_back(Command::Type::ModKeyUp);
    m_cmds.back().m_mods = mod;
  }

  ITextInputCallback* getTextInputCallback() override { return m_rec.getTextInputCallback(); }

  void dispatchEvents() {
    std::unique_lock<std::mutex> lk(m_mt);
    bool destroyed = m_destroyed;
    bool hasResize = m_hasResize;
    if (hasResize)
      m_hasResize = false;
    SWindowRect latestResize = m_latestResize;
    std::vector<Command> cmds;
    m_cmds.swap(cmds);
    lk.unlock();

    if (destroyed) {
      m_rec.destroyed();
      return;
    }

    if (hasResize)
      m_rec.resized(latestResize, false);

    for (const Command& cmd : cmds)
      cmd.dispatch(m_rec);
  }
};

} // namespace boo
