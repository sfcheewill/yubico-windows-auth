//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//

/* This file is based on sample code from the Microsoft Windows SDK
    version 7.1. All changes are Copyright (c) 2012, Yubico AB.
    All rights reserved.
   
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.

   * Redistributions in binary form must reproduce the above copyright
     notice, this list of conditions and the following
     disclaimer in the documentation and/or other materials provided
     with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
   CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES,
   INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
   MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
   DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
   BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
   TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
   ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR
   TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
   THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
   SUCH DAMAGE.
*/


#pragma warning(disable : 4995)

#ifndef WIN32_NO_STATUS
#include <ntstatus.h>
#define WIN32_NO_STATUS
#endif
#include <unknwn.h>

#include "CYkCredential.h"
#include "CWrappedCredentialEvents.h"
#include "guid.h"

#include "YkLib.h"

#include <string>

// CYkCredential ////////////////////////////////////////////////////////

// NOTE: Please read the readme.txt file to understand when it's appropriate to
// wrap an another credential provider and when it's not.  If you have questions
// about whether your scenario is an appropriate use of wrapping another credprov,
// please contact credprov@microsoft.com
CYkCredential::CYkCredential():
    _cRef(1)
{
    DllAddRef();

    ZeroMemory(_rgCredProvFieldDescriptors, sizeof(_rgCredProvFieldDescriptors));
    ZeroMemory(_rgFieldStatePairs, sizeof(_rgFieldStatePairs));
    ZeroMemory(_rgFieldStrings, sizeof(_rgFieldStrings));

    _pWrappedCredential = NULL;
    _pWrappedCredentialEvents = NULL;
    _pCredProvCredentialEvents = NULL;

    _dwWrappedDescriptorCount = 0;
    _dwDatabaseIndex = 0;
}

CYkCredential::~CYkCredential()
{
    for (int i = 0; i < ARRAYSIZE(_rgFieldStrings); i++)
    {
        CoTaskMemFree(_rgFieldStrings[i]);
        CoTaskMemFree(_rgCredProvFieldDescriptors[i].pszLabel);
    }

    _CleanupEvents();
    
    if (_pWrappedCredential)
    {
        _pWrappedCredential->Release();
    }

    DllRelease();
}

// Initializes one credential with the field information passed in. We also keep track
// of our wrapped credential and how many fields it has.
HRESULT CYkCredential::Initialize(
    __in const CREDENTIAL_PROVIDER_FIELD_DESCRIPTOR* rgcpfd,
    __in const FIELD_STATE_PAIR* rgfsp,
    __in ICredentialProviderCredential *pWrappedCredential,
    __in DWORD dwWrappedDescriptorCount
    )
{
    HRESULT hr = S_OK;

    // Grab the credential we're wrapping for future reference.
    if (_pWrappedCredential != NULL)
    {
        _pWrappedCredential->Release();
    }

    _pWrappedCredential = pWrappedCredential;
    _pWrappedCredential->AddRef();

    // We also need to remember how many fields the inner credential has.
    _dwWrappedDescriptorCount = dwWrappedDescriptorCount;

    // Copy the field descriptors for each field. This is useful if you want to vary the field
    // descriptors based on what Usage scenario the credential was created for.
    for (DWORD i = 0; SUCCEEDED(hr) && i < ARRAYSIZE(_rgCredProvFieldDescriptors); i++)
    {
        _rgFieldStatePairs[i] = rgfsp[i];
        hr = FieldDescriptorCopy(rgcpfd[i], &_rgCredProvFieldDescriptors[i]);
    }

    // Initialize the String value of all of our fields.
	if (SUCCEEDED(hr))
    {
		PWSTR username = L"";
		GetStringValue(0, &username);
		if(_CheckEnabled(username)) {
			hr = SHStrDup(L"YubiKey Logon enabled for user.", &_rgFieldStrings[SFI_YUBIKEY_STATUS]);
		} else {
			hr = SHStrDup(L"", &_rgFieldStrings[SFI_YUBIKEY_STATUS]);
		}
		
    }


    return hr;
}

// LogonUI calls this in order to give us a callback in case we need to notify it of 
// anything. We'll also provide it to the wrapped credential.
HRESULT CYkCredential::Advise(
    __in ICredentialProviderCredentialEvents* pcpce
    )
{
    HRESULT hr = S_OK;

    _CleanupEvents();

    // We keep a strong reference on the real ICredentialProviderCredentialEvents
    // to ensure that the weak reference held by the CWrappedCredentialEvents is valid.
    _pCredProvCredentialEvents = pcpce;
    _pCredProvCredentialEvents->AddRef();

    _pWrappedCredentialEvents = new CWrappedCredentialEvents();
    
    if (_pWrappedCredentialEvents != NULL)
    {
        _pWrappedCredentialEvents->Initialize(this, pcpce);
    
        if (_pWrappedCredential != NULL)
        {
            hr = _pWrappedCredential->Advise(_pWrappedCredentialEvents);
        }
    }
    else
    {
        hr = E_OUTOFMEMORY;
    }

    return hr;
}

// LogonUI calls this to tell us to release the callback. 
// We'll also provide it to the wrapped credential.
HRESULT CYkCredential::UnAdvise()
{
    HRESULT hr = S_OK;
    
    if (_pWrappedCredential != NULL)
    {
        _pWrappedCredential->UnAdvise();
    }

    _CleanupEvents();

    return hr;
}

// LogonUI calls this function when our tile is selected (zoomed)
// If you simply want fields to show/hide based on the selected state,
// there's no need to do anything here - you can set that up in the 
// field definitions. In fact, we're just going to hand it off to the
// wrapped credential in case it wants to do something.
HRESULT CYkCredential::SetSelected(__out BOOL* pbAutoLogon)  
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
        hr = _pWrappedCredential->SetSelected(pbAutoLogon);
    }

	*pbAutoLogon = false;

    return hr;
}

// Similarly to SetSelected, LogonUI calls this when your tile was selected
// and now no longer is. We'll let the wrapped credential do anything it needs.
HRESULT CYkCredential::SetDeselected()
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
        hr = _pWrappedCredential->SetDeselected();
    }

    return hr;
}

// Get info for a particular field of a tile. Called by logonUI to get information to 
// display the tile. We'll check to see if it's for us or the wrapped credential, and then
// handle or route it as appropriate.
HRESULT CYkCredential::GetFieldState(
    __in DWORD dwFieldID,
    __out CREDENTIAL_PROVIDER_FIELD_STATE* pcpfs,
    __out CREDENTIAL_PROVIDER_FIELD_INTERACTIVE_STATE* pcpfis
    )
{
    HRESULT hr = E_UNEXPECTED;

    // Make sure we have a wrapped credential.
    if (_pWrappedCredential != NULL)
    {
        // Validate parameters.
        if ((pcpfs != NULL) && (pcpfis != NULL))
        {
            // If the field is in the wrapped credential, hand it off.
            if (_IsFieldInWrappedCredential(dwFieldID))
            {
                hr = _pWrappedCredential->GetFieldState(dwFieldID, pcpfs, pcpfis);
            }
            // Otherwise, we need to see if it's one of ours.
            else
            {
                FIELD_STATE_PAIR *pfsp = _LookupLocalFieldStatePair(dwFieldID);
                // If the field ID is valid, give it info it needs.
                if (pfsp != NULL)
                {
                    *pcpfs = pfsp->cpfs;
                    *pcpfis = pfsp->cpfis;

                    hr = S_OK;
                }
                else
                {
                    hr = E_INVALIDARG;
                }
            }
        }
        else
        {
            hr = E_INVALIDARG;
        }
    }
    return hr;
}

// Sets ppwsz to the string value of the field at the index dwFieldID. We'll check to see if 
// it's for us or the wrapped credential, and then handle or route it as appropriate.
HRESULT CYkCredential::GetStringValue(
    __in DWORD dwFieldID, 
    __deref_out PWSTR* ppwsz
    )
{
    HRESULT hr = E_UNEXPECTED;

    // Make sure we have a wrapped credential.
    if (_pWrappedCredential != NULL)
    {
        // If this field belongs to the wrapped credential, hand it off.
        if (_IsFieldInWrappedCredential(dwFieldID))
        {
            hr = _pWrappedCredential->GetStringValue(dwFieldID, ppwsz);
        }
        // Otherwise determine if we need to handle it.
        else
        {
            FIELD_STATE_PAIR *pfsp = _LookupLocalFieldStatePair(dwFieldID);
			int realId = dwFieldID - _dwWrappedDescriptorCount;
            if (pfsp != NULL)
            {
				if(realId == SFI_YUBIKEY_STATUS) {
					hr = SHStrDupW(_rgFieldStrings[SFI_YUBIKEY_STATUS], ppwsz);
				}
            }
            else
            {
                hr = E_INVALIDARG;
            }
        }
    }
    return hr;
}

// Returns the number of items to be included in the combobox (pcItems), as well as the 
// currently selected item (pdwSelectedItem). We'll check to see if it's for us or the 
// wrapped credential, and then handle or route it as appropriate.
HRESULT CYkCredential::GetComboBoxValueCount(
    __in DWORD dwFieldID, 
    __out DWORD* pcItems, 
    __out_range(<,*pcItems) DWORD* pdwSelectedItem
    )
{
    HRESULT hr = E_UNEXPECTED;

    // Make sure we have a wrapped credential.
    if (_pWrappedCredential != NULL)
    {
        // If this field belongs to the wrapped credential, hand it off.
        if (_IsFieldInWrappedCredential(dwFieldID))
        {
            hr = _pWrappedCredential->GetComboBoxValueCount(dwFieldID, pcItems, pdwSelectedItem);
        }
    }

    return hr;
}

// Called iteratively to fill the combobox with the string (ppwszItem) at index dwItem.
// We'll check to see if it's for us or the wrapped credential, and then handle or route 
// it as appropriate.
HRESULT CYkCredential::GetComboBoxValueAt(
    __in DWORD dwFieldID, 
    __in DWORD dwItem,
    __deref_out PWSTR* ppwszItem
    )
{
    HRESULT hr = E_UNEXPECTED;

    // Make sure we have a wrapped credential.
    if (_pWrappedCredential != NULL)
    {
        // If this field belongs to the wrapped credential, hand it off.
        if (_IsFieldInWrappedCredential(dwFieldID))
        {
            hr = _pWrappedCredential->GetComboBoxValueAt(dwFieldID, dwItem, ppwszItem);
        }
    }

    return hr;
}

// Called when the user changes the selected item in the combobox. We'll check to see if 
// it's for us or the wrapped credential, and then handle or route it as appropriate.
HRESULT CYkCredential::SetComboBoxSelectedValue(
    __in DWORD dwFieldID,
    __in DWORD dwSelectedItem
    )
{
    HRESULT hr = E_UNEXPECTED;

    // Make sure we have a wrapped credential.
    if (_pWrappedCredential != NULL)
    {
        // If this field belongs to the wrapped credential, hand it off.
        if (_IsFieldInWrappedCredential(dwFieldID))
        {
            hr = _pWrappedCredential->SetComboBoxSelectedValue(dwFieldID, dwSelectedItem);
        }
    }

    return hr;
}

//------------- 
// The following methods are for logonUI to get the values of various UI elements and 
// then communicate to the credential about what the user did in that field. Even though
// we don't offer these field types ourselves, we need to pass along the request to the
// wrapped credential.

HRESULT CYkCredential::GetBitmapValue(
    __in DWORD dwFieldID, 
    __out HBITMAP* phbmp
    )
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
        hr = _pWrappedCredential->GetBitmapValue(dwFieldID, phbmp);
    }

    return hr;
}

HRESULT CYkCredential::GetSubmitButtonValue(
    __in DWORD dwFieldID,
    __out DWORD* pdwAdjacentTo
    )
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
        hr = _pWrappedCredential->GetSubmitButtonValue(dwFieldID, pdwAdjacentTo);
    }

    return hr;
}

HRESULT CYkCredential::SetStringValue(
    __in DWORD dwFieldID,
    __in PCWSTR pwz
    )
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
        hr = _pWrappedCredential->SetStringValue(dwFieldID, pwz);
    }

    return hr;

}

HRESULT CYkCredential::GetCheckboxValue(
    __in DWORD dwFieldID, 
    __out BOOL* pbChecked,
    __deref_out PWSTR* ppwszLabel
    )
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
        if (_IsFieldInWrappedCredential(dwFieldID))
        {
            hr = _pWrappedCredential->GetCheckboxValue(dwFieldID, pbChecked, ppwszLabel);
        }
    }

    return hr;
}

HRESULT CYkCredential::SetCheckboxValue(
    __in DWORD dwFieldID, 
    __in BOOL bChecked
    )
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
        hr = _pWrappedCredential->SetCheckboxValue(dwFieldID, bChecked);
    }

    return hr;
}

HRESULT CYkCredential::CommandLinkClicked(__in DWORD dwFieldID)
{
    HRESULT hr = E_UNEXPECTED;

    if (_pWrappedCredential != NULL)
    {
		if(_IsFieldInWrappedCredential(dwFieldID)) {
			hr = _pWrappedCredential->CommandLinkClicked(dwFieldID);
		}
    }

    return hr;
}
//------ end of methods for controls we don't have ourselves ----//


//
// Collect the username and password into a serialized credential for the correct usage scenario 
// (logon/unlock is what's demonstrated in this sample).  LogonUI then passes these credentials 
// back to the system to log on.
//
HRESULT CYkCredential::GetSerialization(
    __out CREDENTIAL_PROVIDER_GET_SERIALIZATION_RESPONSE* pcpgsr,
    __out CREDENTIAL_PROVIDER_CREDENTIAL_SERIALIZATION* pcpcs, 
    __deref_out_opt PWSTR* ppwszOptionalStatusText, 
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
	)
{
	HRESULT hr = E_UNEXPECTED;

	if (_pWrappedCredential != NULL)
	{
		hr = _pWrappedCredential->GetSerialization(pcpgsr, pcpcs, ppwszOptionalStatusText, pcpsiOptionalStatusIcon);
	}

    return hr;
}

// ReportResult is completely optional. However, we will hand it off to the wrapped
// credential in case they want to handle it.
HRESULT CYkCredential::ReportResult(
    __in NTSTATUS ntsStatus, 
    __in NTSTATUS ntsSubstatus,
    __deref_out_opt PWSTR* ppwszOptionalStatusText, 
    __out CREDENTIAL_PROVIDER_STATUS_ICON* pcpsiOptionalStatusIcon
    )
{
    HRESULT hr = E_UNEXPECTED;

	if(ntsStatus == 0xFFFF0001L) { // challenge-response failed
		SHStrDupW(L"YubiKey Logon failed, is there a YubiKey inserted?", ppwszOptionalStatusText);
		*pcpsiOptionalStatusIcon = CPSI_ERROR;
		hr = S_OK;
	} else if(ntsStatus == 0xFFFF0002L) { // wrong response
		SHStrDupW(L"YubiKey Logon failed, please insert correct YubiKey.", ppwszOptionalStatusText);
		*pcpsiOptionalStatusIcon = CPSI_ERROR;
		hr = S_OK;
	}
	else if (_pWrappedCredential != NULL)
    {
        hr = _pWrappedCredential->ReportResult(ntsStatus, ntsSubstatus, ppwszOptionalStatusText, pcpsiOptionalStatusIcon);
    }

    return hr;
}

BOOL CYkCredential::_IsFieldInWrappedCredential(
    __in DWORD dwFieldID
    )
{
    return (dwFieldID < _dwWrappedDescriptorCount);
}

FIELD_STATE_PAIR *CYkCredential::_LookupLocalFieldStatePair(
    __in DWORD dwFieldID
    )
{
    // Offset into the ID to account for the wrapped fields.
    dwFieldID -= _dwWrappedDescriptorCount;

    // If the index if valid, give it the info it wants.
    if (dwFieldID < SFI_NUM_FIELDS)
    {
        return &(_rgFieldStatePairs[dwFieldID]);
    }
    
    return NULL;
}

void CYkCredential::_CleanupEvents()
{
    // Call Uninitialize before releasing our reference on the real 
    // ICredentialProviderCredentialEvents to avoid having an
    // invalid reference.
    if (_pWrappedCredentialEvents != NULL)
    {
        _pWrappedCredentialEvents->Uninitialize();
        _pWrappedCredentialEvents->Release();
        _pWrappedCredentialEvents = NULL;
    }

    if (_pCredProvCredentialEvents != NULL)
    {
        _pCredProvCredentialEvents->Release();
        _pCredProvCredentialEvents = NULL;
    }
}

BOOL CYkCredential::_CheckEnabled(__in LPWSTR username) {
	std::wstring subKey = L"SOFTWARE\\Yubico\\auth\\users\\";
	subKey += username;

	HKEY key;
	DWORD type = REG_DWORD;
	DWORD enabled = 0;
	DWORD dwLen = sizeof(DWORD);

	int res = RegOpenKeyEx(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_READ, &key);
	if(res == ERROR_SUCCESS) {
		res = RegQueryValueExA(key, "enabled", NULL, &type, (LPBYTE)&enabled, &dwLen);
		RegCloseKey(key);
		if(enabled == 1) {
			return true;
		}
	}
	
	return false;
}
