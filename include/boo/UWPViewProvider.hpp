#pragma once

#include "boo/IApplication.hpp"

namespace boo {

#if WINDOWS_STORE
using namespace Windows::ApplicationModel::Core;

ref struct ViewProvider sealed : IFrameworkViewSource {
  internal : ViewProvider(boo::IApplicationCallback& appCb, std::string_view uniqueName, std::string_view friendlyName,
                          std::string_view pname, Platform::Array<Platform::String ^> ^ params, bool singleInstance)
  : m_appCb(appCb)
  , m_uniqueName(uniqueName)
  , m_friendlyName(friendlyName)
  , m_pname(pname)
  , m_singleInstance(singleInstance) {
    char selfPath[1024];
    GetModuleFileNameW(nullptr, selfPath, 1024);
    m_args.reserve(params->Length + 1);
    m_args.emplace_back(selfPath);
    for (Platform::String ^ str : params)
      m_args.emplace_back(str->Data());
  }

public:
  virtual IFrameworkView ^ CreateView();

  internal : boo::IApplicationCallback& m_appCb;
  std::string m_uniqueName;
  std::string m_friendlyName;
  std::string m_pname;
  std::vector<std::string> m_args;
  bool m_singleInstance;
};
#endif

} // namespace boo
