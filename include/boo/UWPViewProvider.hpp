#ifndef UWPVIEWPROVIDER_HPP
#define UWPVIEWPROVIDER_HPP

#include "IApplication.hpp"

namespace boo
{

#if WINDOWS_STORE
using namespace Windows::ApplicationModel::Core;

ref struct ViewProvider sealed : IFrameworkViewSource
{
internal:
    ViewProvider(boo::IApplicationCallback& appCb,
                 SystemStringView uniqueName,
                 SystemStringView friendlyName,
                 SystemStringView pname,
                 Platform::Array<Platform::String^>^ params,
                 bool singleInstance)
    : m_appCb(appCb), m_uniqueName(uniqueName), m_friendlyName(friendlyName),
      m_pname(pname), m_singleInstance(singleInstance)
    {
        SystemChar selfPath[1024];
        GetModuleFileNameW(nullptr, selfPath, 1024);
        m_args.reserve(params->Length + 1);
        m_args.emplace_back(selfPath);
        for (Platform::String^ str : params)
            m_args.emplace_back(str->Data());
    }
public:
    virtual IFrameworkView^ CreateView();

internal:
    boo::IApplicationCallback& m_appCb;
    SystemString m_uniqueName;
    SystemString m_friendlyName;
    SystemString m_pname;
    std::vector<SystemString> m_args;
    bool m_singleInstance;
};
#endif
    
}

#endif // UWPVIEWPROVIDER_HPP
