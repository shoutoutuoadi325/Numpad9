#pragma once
#include <msctf.h>
namespace numpad9::ime {
HRESULT CreateAttributeInfo(ITfDisplayAttributeInfo** result);
HRESULT CreateAttributeEnumerator(IEnumTfDisplayAttributeInfo** result);
}
