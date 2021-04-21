#pragma comment(lib, "d3d9.lib")
#pragma comment(lib, "d3dx9.lib")

#include "DX.h"

#include <d3d9.h>
#include <d3dx9.h>

#include <iostream>
#include <string>

#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"
#include "imgui_internal.h"

#include "Hook.h"

#define LOG
#ifdef LOG
#define LOG_PRINT(x) std::cout << x << std::endl
#else
#define LOG_PRINT(x) ;
#endif

#define HWND_WINDOW_NAME "Tribes: Ascend (32-bit, DX9)"

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using namespace std;
using namespace Hook32;

namespace dx9 {

typedef HRESULT(__stdcall* BeginScene)(LPDIRECT3DDEVICE9);
typedef HRESULT(__stdcall* EndScene)(LPDIRECT3DDEVICE9);
typedef HRESULT(__stdcall* Reset)(LPDIRECT3DDEVICE9, D3DPRESENT_PARAMETERS*);
typedef HRESULT(__stdcall* DrawIndexedPrimitive)(LPDIRECT3DDEVICE9, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT(__stdcall* DrawPrimitive)(LPDIRECT3DDEVICE9, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT(__stdcall* DrawIndexedPrimitiveUP)(LPDIRECT3DDEVICE9, D3DPRIMITIVETYPE, UINT, UINT, UINT, CONST void*, D3DFORMAT, CONST void*, UINT);

typedef HRESULT(__stdcall* SetTexture)(LPDIRECT3DDEVICE9, DWORD, IDirect3DBaseTexture9*);

HRESULT __stdcall BeginSceneHook(LPDIRECT3DDEVICE9 device);
HRESULT __stdcall EndSceneGetDeviceHook(LPDIRECT3DDEVICE9 device);

HRESULT __stdcall EndSceneHook(LPDIRECT3DDEVICE9 device);
HRESULT __stdcall ResetHook(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS* pPresentationParameters);

HRESULT __stdcall DrawIndexedPrimitiveHook(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE primType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount);

HRESULT __stdcall DrawPrimitiveHook(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount);
HRESULT __stdcall DrawIndexedPrimitiveUPHook(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride);

HRESULT __stdcall SetTextureHook(LPDIRECT3DDEVICE9 device, DWORD Stage, IDirect3DBaseTexture9* pTexture);

bool CreateDevice(void);
// bool LoadTextureFromFile(const char* filename, PDIRECT3DTEXTURE9* out_texture, int* out_width, int* out_height);
bool LoadTextureFromFile(const char* filename, class Image** out_image);

LRESULT WINAPI CustomWindowProcCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LPDIRECT3DDEVICE9 game_device = NULL;
static WNDPROC original_windowproc_callback = NULL;
static HWND game_hwnd = NULL;

VMTHook* begin_scene_vmthook;
VMTHook* end_scene_vmthook;
VMTHook* reset_vmthook;
VMTHook* draw_indexed_primitive_vmthook;
VMTHook* draw_primitive_vmthook;
VMTHook* draw_indexed_primitive_up_vmthook;
VMTHook* set_texture_vmthook;
JumpHook* end_scene_get_device_jumphook;

bool show_menu = false;
bool imgui_is_ready = false;

// bool resolution_init = false;
// ImVec2 resolution;

}  // namespace dx9

namespace dx9 {
struct Image {
    PDIRECT3DTEXTURE9 texture_;
    int width_;
    int height_;
    Image(PDIRECT3DTEXTURE9 texture, int width, int height) {
        texture_ = texture;
        width_ = width;
        height_ = height;
    }
}* crosshair = NULL;
}  // namespace dx9

namespace dx9 {
void Hook(void) {
#ifdef LOG
    AllocConsole();
    freopen("CONOUT$", "w", stdout);
#endif

    if (CreateDevice()) {
        LOG_PRINT("Successfully created a LPDIRECT3DDEVICE9 object.");
    } else {
        LOG_PRINT("Failed to create LPDIRECT3DDEVICE9 object.");
    }
}

bool CreateDevice(void) {
    LPDIRECT3D9 direct3d9_object = Direct3DCreate9(D3D_SDK_VERSION);
    if (!direct3d9_object) {
        LOG_PRINT("Attempted to create LPDIRECT3D9 object but Direct3DCreate9 function call failed.");
    }

    HWND hwnd = FindWindowA(NULL, HWND_WINDOW_NAME);
    if (!hwnd) {
        LOG_PRINT("Failed to find hwnd after calling FindWindowA function.");
    }

    game_hwnd = hwnd;

    original_windowproc_callback = (WNDPROC)SetWindowLongPtr(hwnd, GWL_WNDPROC, (LONG_PTR)CustomWindowProcCallback);

    D3DPRESENT_PARAMETERS d3dpresent_parameters;
    ZeroMemory(&d3dpresent_parameters, sizeof(d3dpresent_parameters));
    d3dpresent_parameters.Windowed = TRUE;
    d3dpresent_parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpresent_parameters.hDeviceWindow = hwnd;
    LPDIRECT3DDEVICE9 new_device;
    HRESULT result = direct3d9_object->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpresent_parameters, &new_device);

    if (FAILED(result)) {
        LOG_PRINT("Error creating device after calling CreateDevice function.");
        return false;
    }

    DWORD end_scene_first_instruction_address = VMTHook::GetFunctionFirstInstructionAddress(new_device, 42);

    end_scene_get_device_jumphook = new JumpHook(end_scene_first_instruction_address, (DWORD)EndSceneGetDeviceHook);

    direct3d9_object->Release();
    new_device->Release();

    LOG_PRINT("Sucessfully created a new DX9 device.");

    return true;
}

bool LoadTextureFromFile(const char* filename, Image** out_image) {
    // Load texture from disk
    PDIRECT3DTEXTURE9 texture;
    HRESULT hr = D3DXCreateTextureFromFileA(game_device, filename, &texture);
    if (hr != S_OK)
        return false;

    // Retrieve description of the texture surface so we can access its size
    D3DSURFACE_DESC my_image_desc;
    texture->GetLevelDesc(0, &my_image_desc);

    *out_image = new Image(texture, (int)my_image_desc.Width, (int)my_image_desc.Height);
    return true;
}

LRESULT WINAPI CustomWindowProcCallback(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        if (wParam == VK_F2) {
            show_menu = !show_menu;
        }
    }

    ImGuiIO& io = ::ImGui::GetIO();
    if (show_menu) {
        io.MouseDrawCursor = true;
        ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
        // return true;
    } else {
        io.MouseDrawCursor = false;
    }

    return CallWindowProc(original_windowproc_callback, hWnd, msg, wParam, lParam);
}

HRESULT __stdcall EndSceneGetDeviceHook(LPDIRECT3DDEVICE9 device) {
    LOG_PRINT("In EndSceneGetDeviceHook function.");

    DWORD original_function_address = end_scene_get_device_jumphook->GetHookAddress();

    end_scene_get_device_jumphook->UnHook();

    game_device = device;

    begin_scene_vmthook = new VMTHook(game_device, 41, BeginSceneHook);
    end_scene_vmthook = new VMTHook(game_device, 42, EndSceneHook);
    reset_vmthook = new VMTHook(game_device, 16, ResetHook);
    draw_indexed_primitive_vmthook = new VMTHook(game_device, 82, DrawIndexedPrimitiveHook);
    draw_primitive_vmthook = new VMTHook(game_device, 81, DrawPrimitiveHook);
    draw_indexed_primitive_up_vmthook = new VMTHook(game_device, 84, DrawIndexedPrimitiveUPHook);
    set_texture_vmthook = new VMTHook(game_device, 65, SetTextureHook);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;

    ImGui_ImplWin32_Init(game_hwnd);
    ImGui_ImplDX9_Init(game_device);

    HRESULT res = ((EndScene)original_function_address)(device);

    delete end_scene_get_device_jumphook;

    bool load_texture_result = LoadTextureFromFile("crosshair0.png", &crosshair);
    if (load_texture_result) {
        LOG_PRINT("Successfully loaded texture.");
    } else {
        LOG_PRINT("Failed to load texture.");
    }

    return res;
}

HRESULT __stdcall BeginSceneHook(LPDIRECT3DDEVICE9 device) {
    // LOG_PRINT("In BeginSceneHook function.");

    HRESULT result = ((BeginScene)begin_scene_vmthook->GetOriginalFunction())(device);
    return result;
}

HRESULT __stdcall EndSceneHook(LPDIRECT3DDEVICE9 device) {
    // LOG_PRINT("In EndSceneHook function.");

    ImGui_ImplDX9_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    if (show_menu) {
        ImGui::Begin("test", NULL);
        ImGui::Text("Example Text");
        ImGui::End();

        ImGui::ShowDemoWindow();
    }

    if (crosshair) {
        ImVec2 resolution = ImGui::GetIO().DisplaySize;
        ImVec2 top_left = {resolution.x / 2 - crosshair->width_ / 2, resolution.y / 2 - crosshair->height_ / 2};

        ImGui::SetNextWindowPos(top_left);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("crosshair", NULL, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::Image((void*)crosshair->texture_, ImVec2(crosshair->width_, crosshair->height_));
        ImGui::End();
    }

    ImGui::PopStyleVar();
    ImGui::EndFrame();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());

    HRESULT result = ((EndScene)end_scene_vmthook->GetOriginalFunction())(device);
    return result;
}

HRESULT __stdcall ResetHook(LPDIRECT3DDEVICE9 device, D3DPRESENT_PARAMETERS* pPresentationParameters) {
    ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT result = ((Reset)reset_vmthook->GetOriginalFunction())(device, pPresentationParameters);
    ImGui_ImplDX9_CreateDeviceObjects();
    return result;
}

HRESULT __stdcall DrawIndexedPrimitiveHook(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE primType, INT BaseVertexIndex, UINT MinVertexIndex, UINT NumVertices, UINT startIndex, UINT primCount) {
    string primCount_string = std::to_string(primCount);

    HRESULT result = ((DrawIndexedPrimitive)draw_indexed_primitive_vmthook->GetOriginalFunction())(device, primType, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount);
    return result;
}

HRESULT __stdcall DrawPrimitiveHook(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) {
    HRESULT result = ((DrawPrimitive)draw_primitive_vmthook->GetOriginalFunction())(device, PrimitiveType, StartVertex, PrimitiveCount);
    return result;
}

HRESULT __stdcall DrawIndexedPrimitiveUPHook(LPDIRECT3DDEVICE9 device, D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices, UINT PrimitiveCount, CONST void* pIndexData, D3DFORMAT IndexDataFormat, CONST void* pVertexStreamZeroData, UINT VertexStreamZeroStride) {
    HRESULT result = ((DrawIndexedPrimitiveUP)draw_indexed_primitive_up_vmthook->GetOriginalFunction())(device, PrimitiveType, MinVertexIndex, NumVertices, PrimitiveCount, pIndexData, IndexDataFormat, pVertexStreamZeroData, VertexStreamZeroStride);
    return result;
}

HRESULT __stdcall SetTextureHook(LPDIRECT3DDEVICE9 device, DWORD Stage, IDirect3DBaseTexture9* pTexture) {
    HRESULT result = ((SetTexture)(set_texture_vmthook->GetOriginalFunction()))(device, Stage, pTexture);
    return result;
}

}  // namespace dx9