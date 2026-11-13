//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
// CSampleProvider implements ICredentialProvider, which is the main
// interface that logonUI uses to decide which tiles to display.
//

#include <initguid.h>
#include "CSampleProvider.h"
#include "CSampleCredential.h"
#include "guid.h"
#include "diagnostics.h"

CSampleProvider::CSampleProvider():
    _cRef(1),
    _fRecreateEnumeratedCredentials(false),
    _cpus(CPUS_INVALID),
    _pCredProviderUserArray(nullptr),
    _pEvents(nullptr),
    _upAdviseContext(0),
    _fAutoLogon(false),
    _dwAutoLogonIndex(0)
{
    DllAddRef();
}

CSampleProvider::~CSampleProvider()
{
    UnAdvise();
    _ReleaseEnumeratedCredentials();
    if (_pCredProviderUserArray != nullptr)
    {
        _pCredProviderUserArray->Release();
        _pCredProviderUserArray = nullptr;
    }

    DllRelease();
}

HRESULT CSampleProvider::SetUsageScenario(
    CREDENTIAL_PROVIDER_USAGE_SCENARIO cpus,
    DWORD /*dwFlags*/)
{
    HRESULT hr;

    switch (cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
        _cpus = cpus;
        _fRecreateEnumeratedCredentials = true;
        hr = S_OK;
        break;

    case CPUS_CHANGE_PASSWORD:
    case CPUS_CREDUI:
        hr = E_NOTIMPL;
        break;

    default:
        hr = E_INVALIDARG;
        break;
    }

    return hr;
}

HRESULT CSampleProvider::SetSerialization(
    _In_ CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION const * /*pcpcs*/)
{
    return E_NOTIMPL;
}

// LogonUI calls Advise to provide us a callback interface for notifications.
HRESULT CSampleProvider::Advise(
    _In_ ICredentialProviderEvents *pcpe,
    _In_ UINT_PTR upAdviseContext)
{
    if (!pcpe) return E_POINTER;
    if (_pEvents)
    {
        _pEvents->Release();
    }
    _pEvents = pcpe;
    _pEvents->AddRef();
    _upAdviseContext = upAdviseContext;
    return S_OK;
}

// LogonUI calls UnAdvise when the callback is no longer valid.
HRESULT CSampleProvider::UnAdvise()
{
    if (_pEvents)
    {
        _pEvents->Release();
        _pEvents = nullptr;
    }
    _upAdviseContext = 0;
    return S_OK;
}

HRESULT CSampleProvider::GetFieldDescriptorCount(
    _Out_ DWORD *pdwCount)
{
    if (!pdwCount) return E_POINTER;
    *pdwCount = SFI_NUM_FIELDS;
    return S_OK;
}

HRESULT CSampleProvider::GetFieldDescriptorAt(
    DWORD dwIndex,
    _Outptr_result_nullonfailure_ CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR **ppcpfd)
{
    HRESULT hr;
    if (!ppcpfd) return E_POINTER;
    *ppcpfd = nullptr;

    if ((dwIndex < SFI_NUM_FIELDS) && ppcpfd)
    {
        hr = FieldDescriptorCoAllocCopy(s_rgCredProvFieldDescriptors[dwIndex], ppcpfd);
    }
    else
    {
        hr = E_INVALIDARG;
    }

    return hr;
}

// If *pbAutoLogonWithDefault is TRUE, LogonUI will immediately call
// GetSerialization on the default credential and submit it automatically!
HRESULT CSampleProvider::GetCredentialCount(
    _Out_ DWORD *pdwCount,
    _Out_ DWORD *pdwDefault,
    _Out_ BOOL *pbAutoLogonWithDefault)
{
    if (!pdwCount || !pdwDefault || !pbAutoLogonWithDefault) return E_POINTER;
    LogSampleEnumeration();
    *pdwCount = 0;
    *pdwDefault = CREDENTIAL_PROVIDER_NO_DEFAULT;
    *pbAutoLogonWithDefault = FALSE;

    if (_fRecreateEnumeratedCredentials)
    {
        _fRecreateEnumeratedCredentials = false;
        _ReleaseEnumeratedCredentials();
        _CreateEnumeratedCredentials();
    }

    *pdwCount = static_cast<DWORD>(_rgpCredentials.size());

    // If a pattern was verified, trigger immediate automatic logon with that credential!
    if (_fAutoLogon && _dwAutoLogonIndex < _rgpCredentials.size())
    {
        *pdwDefault = _dwAutoLogonIndex;
        *pbAutoLogonWithDefault = TRUE;
        _fAutoLogon = false;
    }

    return S_OK;
}

HRESULT CSampleProvider::GetCredentialAt(
    DWORD dwIndex,
    _Outptr_result_nullonfailure_ ICredentialProviderCredential **ppcpc)
{
    if (!ppcpc) return E_POINTER;
    *ppcpc = nullptr;

    if (dwIndex < _rgpCredentials.size() && _rgpCredentials[dwIndex])
    {
        return _rgpCredentials[dwIndex]->QueryInterface(IID_PPV_ARGS(ppcpc));
    }
    return E_INVALIDARG;
}

HRESULT CSampleProvider::SetUserArray(_In_ ICredentialProviderUserArray *users)
{
    if (!users) return E_INVALIDARG;
    users->AddRef();
    if (_pCredProviderUserArray)
    {
        _pCredProviderUserArray->Release();
    }
    _pCredProviderUserArray = users;
    _fRecreateEnumeratedCredentials = true;
    return S_OK;
}

void CSampleProvider::TriggerAutoLogon(CSampleCredential* pCred)
{
    for (DWORD i = 0; i < _rgpCredentials.size(); ++i)
    {
        if (_rgpCredentials[i] == pCred)
        {
            _dwAutoLogonIndex = i;
            _fAutoLogon = true;
            if (_pEvents)
            {
                _pEvents->CredentialsChanged(_upAdviseContext);
            }
            break;
        }
    }
}

void CSampleProvider::_CreateEnumeratedCredentials()
{
    switch (_cpus)
    {
    case CPUS_LOGON:
    case CPUS_UNLOCK_WORKSTATION:
        {
            _EnumerateCredentials();
            break;
        }
    default:
        break;
    }
}

void CSampleProvider::_ReleaseEnumeratedCredentials()
{
    for (CSampleCredential* pCred : _rgpCredentials)
    {
        if (pCred != nullptr)
        {
            pCred->Release();
        }
    }
    _rgpCredentials.clear();
}

HRESULT CSampleProvider::_EnumerateCredentials()
{
    HRESULT hr = E_UNEXPECTED;
    if (_pCredProviderUserArray != nullptr)
    {
        DWORD dwUserCount = 0;
        hr = _pCredProviderUserArray->GetCount(&dwUserCount);
        if (FAILED(hr)) return hr;
        for (DWORD i = 0; i < dwUserCount; ++i)
        {
            ICredentialProviderUser *pCredUser = nullptr;
            hr = _pCredProviderUserArray->GetAt(i, &pCredUser);
            if (SUCCEEDED(hr) && pCredUser)
            {
                CSampleCredential *pCredential = new(std::nothrow) CSampleCredential();
                if (pCredential != nullptr)
                {
                    hr = pCredential->Initialize(_cpus, s_rgCredProvFieldDescriptors, s_rgFieldStatePairs, pCredUser);
                    if (SUCCEEDED(hr))
                    {
                        pCredential->SetProvider(this);
                        _rgpCredentials.push_back(pCredential);
                    }
                    else
                    {
                        pCredential->Release();
                    }
                }
                pCredUser->Release();
            }
        }
    }
    return hr;
}

// Boilerplate code to create our provider.
HRESULT CSample_CreateInstance(_In_ REFIID riid, _Outptr_ void **ppv)
{
    HRESULT hr;
    CSampleProvider* pProvider = new(std::nothrow) CSampleProvider();
    if (pProvider)
    {
        hr = pProvider->QueryInterface(riid, ppv);
        pProvider->Release();
    }
    else
    {
        hr = E_OUTOFMEMORY;
    }
    return hr;
}
