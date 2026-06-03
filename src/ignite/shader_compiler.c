#include "kerosene.h"

#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>

int kerosene_probe_d3d11(char *out, size_t cap) {
    if (out && cap) {
        out[0] = 0;
    }
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    D3D_FEATURE_LEVEL got = 0;
    HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels, (UINT)(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &device, &got, &context);
    if (FAILED(hr)) {
        if (out && cap) snprintf(out, cap, "D3D11 hardware indisponivel (0x%08lx)", (unsigned long)hr);
        return 0;
    }
    if (out && cap) snprintf(out, cap, "D3D11 feature level 0x%04x", (unsigned)got);
    if (context) ID3D11DeviceContext_Release(context);
    if (device) ID3D11Device_Release(device);
    return 1;
}
