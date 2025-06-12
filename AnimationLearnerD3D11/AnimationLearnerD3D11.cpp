#include <windows.h>
#include <d3d11.h>
#pragma comment(lib, "d3d11.lib")

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "App.h"
#include <memory>
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib") 

// For std::cout
#include <iostream>
#include <vector>
#include <functional>
#include <algorithm>

#include <map>
#include <string>
#include <chrono>


// 全局变量
HWND g_hWnd = nullptr;
D3D_FEATURE_LEVEL g_featureLevel = D3D_FEATURE_LEVEL_11_0;
ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pImmediateContext = nullptr;
IDXGISwapChain* g_pSwapChain = nullptr;
ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
ID3D11Texture2D* g_pDepthStencil = nullptr;
ID3D11DepthStencilView* g_pDepthStencilView = nullptr;
ID3D11DepthStencilState* g_pDepthStencilState = nullptr;
std::chrono::steady_clock::time_point g_startTime;
ID3D11RasterizerState* g_pRasterState_NoCull = nullptr;

aiVector3D IK_Position = { 8.2,  21, 5.0 }; // IK目标位置

Assimp::Importer importer;

UINT g_width = 1024, g_height = 768; // 全局变量

void UpdateConstant(App* App, float time);

void RedirectIOToConsole()
{
    AllocConsole();
    FILE* fp;
    freopen_s(&fp, "CONOUT$", "w", stdout);
    freopen_s(&fp, "CONOUT$", "w", stderr);
    freopen_s(&fp, "CONIN$", "r", stdin);
    std::ios::sync_with_stdio();
}

// 线性插值
aiVector3D Lerp(const aiVector3D& a, const aiVector3D& b, float t) {
    return a + (b - a) * t;
}

// 四元数球面插值
aiQuaternion Slerp(const aiQuaternion& a, const aiQuaternion& b, float t) {
    aiQuaternion out;
    aiQuaternion::Interpolate(out, a, b, t);
    return out;
}

// 查找关键帧索引
template<typename T>
size_t FindKeyIndex(const std::vector<T>& keys, float time) {
    for (size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time < static_cast<float>(keys[i + 1].mTime))
            return i;
    }
    return keys.size() - 2;
}

void ResizeRenderTarget(UINT width, UINT height)
{
    g_width = width;
    g_height = height;

    if (!g_pSwapChain) return;

    // 释放旧的视图和缓冲区
    if (g_pImmediateContext) g_pImmediateContext->OMSetRenderTargets(0, 0, 0);
    if (g_pRenderTargetView) { g_pRenderTargetView->Release(); g_pRenderTargetView = nullptr; }
    if (g_pDepthStencilView) { g_pDepthStencilView->Release(); g_pDepthStencilView = nullptr; }
    if (g_pDepthStencil) { g_pDepthStencil->Release();     g_pDepthStencil = nullptr; }

    // 调整交换链缓冲区大小
    HRESULT hr = g_pSwapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) return;

    // 重新获取后备缓冲区并创建RTV
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr)) return;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr)) return;

    // 创建新的深度缓冲区和视图
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &g_pDepthStencil);
    if (FAILED(hr)) return;

    hr = g_pd3dDevice->CreateDepthStencilView(g_pDepthStencil, nullptr, &g_pDepthStencilView);
    if (FAILED(hr)) return;

    // 重新绑定渲染目标
    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, g_pDepthStencilView);

    // 更新视口
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports(1, &vp);
}


// 窗口过程函数
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_SIZE:
        if (g_pSwapChain && wParam != SIZE_MINIMIZED)
        {
            UINT width = LOWORD(lParam);
            UINT height = HIWORD(lParam);
            // 这里调用你自定义的重建RTV/DSV的函数
            ResizeRenderTarget(width, height);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

// 初始化窗口
HRESULT InitWindow(HINSTANCE hInstance, int nCmdShow)
{
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0, 0,
                      GetModuleHandle(NULL), nullptr, nullptr, nullptr, nullptr,
                      L"DX11SampleClass", nullptr };
    RegisterClassEx(&wc);

    g_hWnd = CreateWindow(wc.lpszClassName, L"FBX Animation Loader",
        WS_OVERLAPPEDWINDOW, 100, 100, 1024, 768,
        nullptr, nullptr, wc.hInstance, nullptr);

    if (!g_hWnd)
        return E_FAIL;

    ShowWindow(g_hWnd, nCmdShow);
    UpdateWindow(g_hWnd);

    RedirectIOToConsole();

    return S_OK;
}

// 初始化 DirectX 11
HRESULT InitD3D()
{
    RECT rc;
    GetClientRect(g_hWnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 1;
    sd.BufferDesc.Width = width;
    sd.BufferDesc.Height = height;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = g_hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                    // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &sd,
        &g_pSwapChain,
        &g_pd3dDevice,
        &g_featureLevel,
        &g_pImmediateContext);

    if (FAILED(hr))
        return hr;

    // 创建渲染目标视图
    ID3D11Texture2D* pBackBuffer = nullptr;
    hr = g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
    if (FAILED(hr))
        return hr;

    hr = g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
    pBackBuffer->Release();
    if (FAILED(hr))
        return hr;

    //g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);


    // 1. 创建深度缓冲区纹理
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width = width;
    depthDesc.Height = height;
    depthDesc.MipLevels = 1;
    depthDesc.ArraySize = 1;
    depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.SampleDesc.Quality = 0;
    depthDesc.Usage = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    hr = g_pd3dDevice->CreateTexture2D(&depthDesc, nullptr, &g_pDepthStencil);
    if (FAILED(hr))
        return hr;

    // 2. 创建深度视图
    hr = g_pd3dDevice->CreateDepthStencilView(g_pDepthStencil, nullptr, &g_pDepthStencilView);
    if (FAILED(hr))
        return hr;

    // 3. 设置为渲染目标（颜色 + 深度）
    g_pImmediateContext->OMSetRenderTargets(1, &g_pRenderTargetView, g_pDepthStencilView);

    // 设置视口
    D3D11_VIEWPORT vp;
    vp.Width = (FLOAT)width;
    vp.Height = (FLOAT)height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    g_pImmediateContext->RSSetViewports(1, &vp);



    return S_OK;
}

// 清理资源
void Cleanup()
{
    if (g_pImmediateContext) g_pImmediateContext->ClearState();
    if (g_pRenderTargetView) g_pRenderTargetView->Release();
    if (g_pSwapChain) g_pSwapChain->Release();
    if (g_pImmediateContext) g_pImmediateContext->Release();
    if (g_pd3dDevice) g_pd3dDevice->Release();
}

// 主消息循环和渲染
int Run(App* app)
{
    MSG msg = { 0 };
    g_startTime = std::chrono::steady_clock::now();

    while (msg.message != WM_QUIT)
    {
        if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {

            auto now = std::chrono::steady_clock::now();
            float time = std::chrono::duration<float>(now - g_startTime).count();
            UpdateConstant(app, time);

            // 清屏颜色
            float clearColor[4] = { 0.2f, 0.4f, 0.6f, 1.0f };
            g_pImmediateContext->ClearRenderTargetView(g_pRenderTargetView, clearColor);
            g_pImmediateContext->ClearDepthStencilView(g_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

            // 1. 设置着色器和输入布局
            g_pImmediateContext->VSSetShader(app->vertexShader, nullptr, 0);
            g_pImmediateContext->PSSetShader(app->pixelShader, nullptr, 0);
            g_pImmediateContext->IASetInputLayout(app->inputLayout);

            // 2. 设置缓冲区
            UINT stride = sizeof(Vertex);
            UINT offset = 0;
            g_pImmediateContext->IASetVertexBuffers(0, 1, &app->vertexBuffer, &stride, &offset);
            g_pImmediateContext->IASetIndexBuffer(app->indexBuffer, DXGI_FORMAT_R32_UINT, 0);
            g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            // 3. 更新常量缓冲区
            ConstantBuffer cb;
            cb.world = DirectX::XMMatrixTranspose(app->cb.world);
            cb.view = DirectX::XMMatrixTranspose(app->cb.view);
            cb.proj = DirectX::XMMatrixTranspose(app->cb.proj);
            cb.lightDir = DirectX::XMFLOAT3(-0.5f, -1.0f, -0.3f); // 示例光照方向

            g_pImmediateContext->UpdateSubresource(app->constantBuffer, 0, nullptr, &app->cb, 0, 0);

            // 4. 绑定常量缓冲区到 Shader
            g_pImmediateContext->VSSetConstantBuffers(0, 1, &app->constantBuffer);
            g_pImmediateContext->PSSetConstantBuffers(0, 1, &app->constantBuffer);

            // 5. 绘制
            g_pImmediateContext->DrawIndexed((UINT)app->indices.size(), 0, 0);

            // 2. 渲染骨骼线
            // 禁用深度测试
            ID3D11DepthStencilState* pOldDS = nullptr;
            UINT oldStencilRef = 0;
            g_pImmediateContext->OMGetDepthStencilState(&pOldDS, &oldStencilRef);

            D3D11_DEPTH_STENCIL_DESC dsDesc = {};
            dsDesc.DepthEnable = FALSE; // 关闭深度
            dsDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
            dsDesc.DepthFunc = D3D11_COMPARISON_ALWAYS;
            dsDesc.StencilEnable = FALSE;

            ID3D11DepthStencilState* pNoDepthDS = nullptr;
            g_pd3dDevice->CreateDepthStencilState(&dsDesc, &pNoDepthDS);
            g_pImmediateContext->OMSetDepthStencilState(pNoDepthDS, 0);

            // 设置骨骼线shader
            g_pImmediateContext->VSSetShader(app->boneLineVS, nullptr, 0);
            g_pImmediateContext->PSSetShader(app->boneLinePS, nullptr, 0);
            g_pImmediateContext->IASetInputLayout(app->boneLineLayout);

            // 绑定骨骼线顶点缓冲区
            UINT stride2 = sizeof(aiVector3D);
            UINT offset2 = 0;
            g_pImmediateContext->IASetVertexBuffers(0, 1, &app->boneLineVB, &stride2, &offset2);
            g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

            // 常量缓冲区（用同一个即可）
            g_pImmediateContext->VSSetConstantBuffers(0, 1, &app->constantBuffer);

            // 绘制
            g_pImmediateContext->Draw(app->boneLineVertexCount, 0);

            if (app->ikQuadVB)
            {
                UINT stride = sizeof(aiVector3D);
                UINT offset = 0;
                g_pImmediateContext->IASetVertexBuffers(0, 1, &app->ikQuadVB, &stride, &offset);
                g_pImmediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                // 设置双面渲染
                g_pImmediateContext->RSSetState(g_pRasterState_NoCull);

                g_pImmediateContext->VSSetShader(app->boneLineVS, nullptr, 0);
                g_pImmediateContext->PSSetShader(app->boneLinePS, nullptr, 0);
                g_pImmediateContext->IASetInputLayout(app->boneLineLayout);

                g_pImmediateContext->VSSetConstantBuffers(0, 1, &app->constantBuffer);
                g_pImmediateContext->Draw(36, 0);
            }

            // 恢复深度状态
            g_pImmediateContext->OMSetDepthStencilState(pOldDS, oldStencilRef);
            if (pOldDS) pOldDS->Release();
            if (pNoDepthDS) pNoDepthDS->Release();

            // Present
            g_pSwapChain->Present(1, 0);
        }
    }
    return (int)msg.wParam;
}

// 递归遍历节点，收集骨骼节点的世界变换
void CollectBonePositions(aiNode* node, const aiMatrix4x4& parentTransform,
    std::map<std::string, aiVector3D>& bonePositions)
{
    aiMatrix4x4 globalTransform = parentTransform * node->mTransformation;
    bonePositions[node->mName.C_Str()] = aiVector3D(globalTransform.a4, globalTransform.b4, globalTransform.c4);

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        CollectBonePositions(node->mChildren[i], globalTransform, bonePositions);
}

// 收集骨骼连线
void CollectBoneLines(aiNode* node, const std::map<std::string, aiVector3D>& bonePositions,
    std::vector<aiVector3D>& lineVertices)
{
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        aiNode* child = node->mChildren[i];
        // 只要父子都在骨骼表里，就连线
        if (bonePositions.count(node->mName.C_Str()) && bonePositions.count(child->mName.C_Str()))
        {
            lineVertices.push_back(bonePositions.at(node->mName.C_Str()));
            lineVertices.push_back(bonePositions.at(child->mName.C_Str()));
        }
        CollectBoneLines(child, bonePositions, lineVertices);
    }
}

void CollectAnimatedBonePositions(
    aiNode* node,
    const aiMatrix4x4& parentTransform,
    const std::map<std::string, BoneAnimCache>& boneAnimCache,
    float animTime,
    std::map<std::string, aiVector3D>& bonePositions)
{
    // 默认用节点原始变换
    aiMatrix4x4 localTransform = node->mTransformation;

    // 如果有动画通道，插值动画
    auto it = boneAnimCache.find(node->mName.C_Str());
    if (it != boneAnimCache.end()) {
        const BoneAnimCache& cache = it->second;

        // 插值位置
        aiVector3D pos(0, 0, 0);
        if (!cache.positions.empty()) {
            if (cache.positions.size() == 1) {
                pos = cache.positions[0].mValue;
            }
            else {
                size_t idx = FindKeyIndex(cache.positions, animTime);
                float t = float((animTime - cache.positions[idx].mTime) /
                    (cache.positions[idx + 1].mTime - cache.positions[idx].mTime));
                pos = Lerp(cache.positions[idx].mValue, cache.positions[idx + 1].mValue, t);
            }
        }

        // 插值旋转
        aiQuaternion rot;
        if (!cache.rotations.empty()) {
            if (cache.rotations.size() == 1) {
                rot = cache.rotations[0].mValue;
            }
            else {
                size_t idx = FindKeyIndex(cache.rotations, animTime);
                float t = float((animTime - cache.rotations[idx].mTime) /
                    (cache.rotations[idx + 1].mTime - cache.rotations[idx].mTime));
                rot = Slerp(cache.rotations[idx].mValue, cache.rotations[idx + 1].mValue, t);
            }
        }

        // 插值缩放
        aiVector3D scale(1, 1, 1);
        if (!cache.scalings.empty()) {
            if (cache.scalings.size() == 1) {
                scale = cache.scalings[0].mValue;
            }
            else {
                size_t idx = FindKeyIndex(cache.scalings, animTime);
                float t = float((animTime - cache.scalings[idx].mTime) /
                    (cache.scalings[idx + 1].mTime - cache.scalings[idx].mTime));
                scale = Lerp(cache.scalings[idx].mValue, cache.scalings[idx + 1].mValue, t);
            }
        }

        // 组装本地变换
        aiMatrix4x4 matScale, matRot, matTrans;
        aiMatrix4x4::Scaling(scale, matScale);
        matRot = aiMatrix4x4(rot.GetMatrix());
        aiMatrix4x4::Translation(pos, matTrans);
        localTransform = matTrans * matRot * matScale;
    }

    aiMatrix4x4 globalTransform = parentTransform * localTransform;
    bonePositions[node->mName.C_Str()] = aiVector3D(globalTransform.a4, globalTransform.b4, globalTransform.c4);

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        CollectAnimatedBonePositions(node->mChildren[i], globalTransform, boneAnimCache, animTime, bonePositions);

}

void CollectAnimatedBoneMatrices(
    aiNode* node,
    const aiMatrix4x4& parentTransform,
    const std::map<std::string, BoneAnimCache>& boneAnimCache,
    const std::map<std::string, int>& boneNameToIndex,
    const std::map<std::string, aiMatrix4x4>& boneOffsetMatrices,
    float animTime,
    std::map<std::string, aiMatrix4x4>& nodeGlobalTransforms, // 可选，调试用
    DirectX::XMMATRIX* outBoneMatrices, // 128个
    int maxBones
)
{
    // 1. 计算本地变换（和你原来的 CollectAnimatedBonePositions 一样）
    aiMatrix4x4 localTransform = node->mTransformation;
    auto it = boneAnimCache.find(node->mName.C_Str());
    if (it != boneAnimCache.end()) {
        const BoneAnimCache& cache = it->second;
        // ...（插值代码同你原来的 CollectAnimatedBonePositions）...
        aiVector3D pos(0, 0, 0);
        if (!cache.positions.empty()) {
            if (cache.positions.size() == 1) pos = cache.positions[0].mValue;
            else {
                size_t idx = FindKeyIndex(cache.positions, animTime);
                float t = float((animTime - cache.positions[idx].mTime) /
                    (cache.positions[idx + 1].mTime - cache.positions[idx].mTime));
                pos = Lerp(cache.positions[idx].mValue, cache.positions[idx + 1].mValue, t);
            }
        }
        aiQuaternion rot;
        if (!cache.rotations.empty()) {
            if (cache.rotations.size() == 1) rot = cache.rotations[0].mValue;
            else {
                size_t idx = FindKeyIndex(cache.rotations, animTime);
                float t = float((animTime - cache.rotations[idx].mTime) /
                    (cache.rotations[idx + 1].mTime - cache.rotations[idx].mTime));
                rot = Slerp(cache.rotations[idx].mValue, cache.rotations[idx + 1].mValue, t);
            }
        }
        aiVector3D scale(1, 1, 1);
        if (!cache.scalings.empty()) {
            if (cache.scalings.size() == 1) scale = cache.scalings[0].mValue;
            else {
                size_t idx = FindKeyIndex(cache.scalings, animTime);
                float t = float((animTime - cache.scalings[idx].mTime) /
                    (cache.scalings[idx + 1].mTime - cache.scalings[idx].mTime));
                scale = Lerp(cache.scalings[idx].mValue, cache.scalings[idx + 1].mValue, t);
            }
        }
        aiMatrix4x4 matScale, matRot, matTrans;
        aiMatrix4x4::Scaling(scale, matScale);
        matRot = aiMatrix4x4(rot.GetMatrix());
        aiMatrix4x4::Translation(pos, matTrans);
        localTransform = matTrans * matRot * matScale;
    }

    // 2. 计算全局变换
    aiMatrix4x4 globalTransform = parentTransform * localTransform;
    nodeGlobalTransforms[node->mName.C_Str()] = globalTransform;

    // 3. 如果是骨骼节点，计算最终蒙皮矩阵
    auto idxIt = boneNameToIndex.find(node->mName.C_Str());
    if (idxIt != boneNameToIndex.end()) {
        int boneIdx = idxIt->second;
        if (boneIdx < maxBones) {
            // offsetMatrix * globalTransform
            aiMatrix4x4 offset = boneOffsetMatrices.at(node->mName.C_Str());
            aiMatrix4x4 finalTransform = globalTransform * offset;

            // 转成 XMMATRIX
            outBoneMatrices[boneIdx] = DirectX::XMMATRIX(
                finalTransform.a1, finalTransform.b1, finalTransform.c1, finalTransform.d1,
                finalTransform.a2, finalTransform.b2, finalTransform.c2, finalTransform.d2,
                finalTransform.a3, finalTransform.b3, finalTransform.c3, finalTransform.d3,
                finalTransform.a4, finalTransform.b4, finalTransform.c4, finalTransform.d4
            );
            outBoneMatrices[boneIdx] = DirectX::XMMatrixTranspose(outBoneMatrices[boneIdx]);
        }
    }

    // 递归
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        CollectAnimatedBoneMatrices(
            node->mChildren[i], globalTransform,
            boneAnimCache, boneNameToIndex, boneOffsetMatrices,
            animTime, nodeGlobalTransforms, outBoneMatrices, maxBones
        );
}

// 递归打印aiNode信息
void PrintNodeInfo(aiNode* node, int depth = 0)
{
    // 缩进
    for (int i = 0; i < depth; ++i) std::cout << "  ";

    // 打印节点名
    std::cout << "Node: " << (node->mName.length > 0 ? node->mName.C_Str() : "[Unnamed]");

    // 打印mesh索引
    if (node->mNumMeshes > 0) {
        std::cout << " | Meshes: ";
        for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
            std::cout << node->mMeshes[i];
            if (i + 1 < node->mNumMeshes) std::cout << ", ";
        }
    }

    // 打印子节点数
    std::cout << " | Children: " << node->mNumChildren << std::endl;

    // 递归打印子节点
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        PrintNodeInfo(node->mChildren[i], depth + 1);
    }
}

bool LoadModel(const std::string& filePath, App* App)
{


    App->scene = const_cast<aiScene*>(importer.ReadFile(
        filePath,
        aiProcess_Triangulate | aiProcess_ConvertToLeftHanded |
        aiProcess_JoinIdenticalVertices | aiProcess_GenNormals
    ));

    if (!App->scene || App->scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !App->scene->mRootNode)
    {
        MessageBoxA(nullptr, importer.GetErrorString(), "Assimp Error", MB_OK | MB_ICONERROR);
        return false;
    }

    // 全局清空
    App->boneNameToIndex.clear();
    App->boneOffsetMatrices.clear();
    App->vertices.clear();
    App->indices.clear();

    int boneCount = 0;
    for (unsigned int i = 0; i < App->scene->mNumMeshes; ++i)
    {
        aiMesh* mesh = App->scene->mMeshes[i];

        size_t baseVertex = App->vertices.size();

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
        {
            aiVector3D pos = mesh->mVertices[v];
            aiVector3D normal = mesh->HasNormals() ? mesh->mNormals[v] : aiVector3D(0, 0, 0);
            aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][v] : aiVector3D(0, 0, 0);

            Vertex vert;
            vert.position = { pos.x, pos.y, pos.z };
            vert.normal = { normal.x, normal.y, normal.z };
            vert.texcoord = { uv.x, uv.y };
            for (int i = 0; i < 4; ++i) {
                vert.boneIndices[i] = 0;
                vert.boneWeights[i] = 0.0f;
            }
            App->vertices.push_back(vert);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j)
            {
                App->indices.push_back(baseVertex + face.mIndices[j]);
            }
        }

        // 代码加在这里，打印aiMesh* mesh的信息
        // 打印该 mesh 的所有骨骼名称和位置
        if (mesh->HasBones()) {
            std::cout << "Mesh " << i << " ("
                << (mesh->mName.length > 0 ? mesh->mName.C_Str() : "[Unnamed]")
                << ") has " << mesh->mNumBones << " bones:" << std::endl;
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                aiBone* bone = mesh->mBones[b];
                // mOffsetMatrix 的 a4, b4, c4 分别是 x, y, z 平移分量
                const aiMatrix4x4& m = bone->mOffsetMatrix;
                float x = m.a4, y = m.b4, z = m.c4;
                std::cout << "  Bone " << b << ": "
                    << (bone->mName.length > 0 ? bone->mName.C_Str() : "[Unnamed]")
                    << "  Offset(pos): (" << x << ", " << y << ", " << z << ")"
                    << std::endl;
            }

            // 1. 建立骨骼名到索引的映射
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                std::string boneName = mesh->mBones[b]->mName.C_Str();
                if (App->boneNameToIndex.count(boneName) == 0)
                    App->boneNameToIndex[boneName] = boneCount++;
            }

            // 2. 填充顶点的骨骼索引和权重
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                int boneIndex = App->boneNameToIndex[mesh->mBones[b]->mName.C_Str()];
                for (unsigned int w = 0; w < mesh->mBones[b]->mNumWeights; ++w) {
                    unsigned int vId = mesh->mBones[b]->mWeights[w].mVertexId + baseVertex;
                    float weight = mesh->mBones[b]->mWeights[w].mWeight;
                    Vertex& v = App->vertices[vId];
                    for (int i = 0; i < 4; ++i) {
                        if (v.boneWeights[i] == 0.0f) {
                            v.boneIndices[i] = boneIndex;
                            v.boneWeights[i] = weight;
                            break;
                        }
                    }
                }
            }

            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                std::string boneName = mesh->mBones[b]->mName.C_Str();
                App->boneOffsetMatrices[boneName] = mesh->mBones[b]->mOffsetMatrix;
            }
        }
    }

    // 1. 收集所有骨骼节点的世界空间位置
    std::map<std::string, aiVector3D> bonePositions;
    CollectBonePositions(App->scene->mRootNode, aiMatrix4x4(), bonePositions);

    // 2. 生成骨骼连线顶点
    std::vector<aiVector3D> boneLines;
    CollectBoneLines(App->scene->mRootNode, bonePositions, boneLines);

    // 3. 创建线段顶点缓冲区
    if (!boneLines.empty()) {
        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = UINT(sizeof(aiVector3D) * boneLines.size());
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = boneLines.data();
        ID3D11Buffer* boneLineVB = nullptr;
        g_pd3dDevice->CreateBuffer(&bd, &initData, &boneLineVB);

        // 保存到 App 结构体里，渲染时用
        App->boneLineVB = boneLineVB;
        App->boneLineVertexCount = boneLines.size();
    }


    // 打印动画信息
    if (App->scene->HasAnimations()) {
        const aiAnimation* anim = App->scene->mAnimations[0];

        App->animDuration = static_cast<float>(anim->mDuration);
        App->animTicksPerSecond = anim->mTicksPerSecond > 0 ? static_cast<float>(anim->mTicksPerSecond) : 25.0f;

        for (unsigned int ch = 0; ch < anim->mNumChannels; ++ch) {
            const aiNodeAnim* channel = anim->mChannels[ch];
            BoneAnimCache cache;
            cache.positions.assign(channel->mPositionKeys, channel->mPositionKeys + channel->mNumPositionKeys);
            cache.rotations.assign(channel->mRotationKeys, channel->mRotationKeys + channel->mNumRotationKeys);
            cache.scalings.assign(channel->mScalingKeys, channel->mScalingKeys + channel->mNumScalingKeys);
            App->boneAnimCache[channel->mNodeName.C_Str()] = std::move(cache);
        }

    }

    if (App->scene && App->scene->mRootNode) {
        std::cout << "==== Scene Node Hierarchy ====" << std::endl;
        PrintNodeInfo(App->scene->mRootNode);
        std::cout << "==============================" << std::endl;
    }

    App->vbd = {};
    App->vbd.Usage = D3D11_USAGE_DEFAULT;
    App->vbd.ByteWidth = sizeof(Vertex) * App->vertices.size();
    App->vbd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    App->vinitData = {};
    App->vinitData.pSysMem = App->vertices.data();

    App->vertexBuffer = nullptr;
    g_pd3dDevice->CreateBuffer(&App->vbd, &App->vinitData, &App->vertexBuffer);

    App->ibd = {};
    App->ibd.Usage = D3D11_USAGE_DEFAULT;
    App->ibd.ByteWidth = sizeof(UINT) * App->indices.size();
    App->ibd.BindFlags = D3D11_BIND_INDEX_BUFFER;

    App->iinitData = {};
    App->iinitData.pSysMem = App->indices.data();

    App->indexBuffer = nullptr;
    g_pd3dDevice->CreateBuffer(&App->ibd, &App->iinitData, &App->indexBuffer);

    App->constantBuffer = nullptr;
    App->cbd = {};
    App->cbd.Usage = D3D11_USAGE_DEFAULT;
    App->cbd.ByteWidth = sizeof(ConstantBuffer);
    App->cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    App->cbd.CPUAccessFlags = 0;
    g_pd3dDevice->CreateBuffer(&App->cbd, nullptr, &App->constantBuffer);

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(BoneMatrixBuffer);
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bd.CPUAccessFlags = 0;
    g_pd3dDevice->CreateBuffer(&bd, nullptr, &App->boneMatrixBuffer);

    return true;
}


bool InitShaders(App* app)
{
    // 编译 Vertex Shader
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;
    HRESULT hr = D3DCompileFromFile(
        L"data/PhongShader.hlsl",
        nullptr, nullptr,
        "VSMain", "vs_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &vsBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        return false;
    }

    // 创建 Vertex Shader
    hr = g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &app->vertexShader);
    if (FAILED(hr))
    {
        vsBlob->Release();
        return false;
    }

    // 编译 Pixel Shader
    ID3DBlob* psBlob = nullptr;
    hr = D3DCompileFromFile(
        L"data/PhongShader.hlsl",
        nullptr, nullptr,
        "PSMain", "ps_5_0",
        D3DCOMPILE_ENABLE_STRICTNESS, 0,
        &psBlob, &errorBlob);

    if (FAILED(hr))
    {
        if (errorBlob)
        {
            OutputDebugStringA((char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }
        vsBlob->Release();
        return false;
    }

    hr = g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &app->pixelShader);
    psBlob->Release();
    if (FAILED(hr))
    {
        vsBlob->Release();
        return false;
    }

    // 定义顶点输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,                             D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, sizeof(float) * 3,              D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, sizeof(float) * 6,              D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BONEINDICES", 0, DXGI_FORMAT_R32G32B32A32_UINT,  0, sizeof(float) * 8,           D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BONEWEIGHTS", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, sizeof(float) * 8 + sizeof(UINT) * 4, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = g_pd3dDevice->CreateInputLayout(
        layout, ARRAYSIZE(layout),
        vsBlob->GetBufferPointer(),
        vsBlob->GetBufferSize(),
        &app->inputLayout);
    vsBlob->Release();

    if (FAILED(hr))
        return false;

    return true;
}

bool InitBoneLineShader(App* app)
{
    ID3DBlob* vsBlob = nullptr;
    ID3DBlob* psBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompileFromFile(
        L"data/BoneLineShader.hlsl", nullptr, nullptr,
        "VSMain", "vs_5_0", 0, 0, &vsBlob, &errorBlob);
    if (FAILED(hr)) return false;

    hr = g_pd3dDevice->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &app->boneLineVS);
    if (FAILED(hr)) { vsBlob->Release(); return false; }

    hr = D3DCompileFromFile(
        L"data/BoneLineShader.hlsl", nullptr, nullptr,
        "PSMain", "ps_5_0", 0, 0, &psBlob, &errorBlob);
    if (FAILED(hr)) { vsBlob->Release(); return false; }

    hr = g_pd3dDevice->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &app->boneLinePS);
    if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); return false; }

    // 输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    hr = g_pd3dDevice->CreateInputLayout(layout, 1, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &app->boneLineLayout);

    vsBlob->Release();
    psBlob->Release();

    return SUCCEEDED(hr);
}


void UpdateConstant(App* App, float time)
{
    DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();

    // 圆周运动参数
    float radius = 400.0f;
    float height = 250.0f;
    float speed = 0.5f; // 每秒转多少圈（弧度/秒）

    // 计算相机位置
    float angle = speed * time; // 弧度
    float camX = radius * sinf(angle);
    float camZ = radius * cosf(angle);
    float camY = height;

    DirectX::XMVECTOR eyePosition = DirectX::XMVectorSet(camX, camY, camZ, 1.0f);

    // 看向模型中心
    DirectX::XMVECTOR focusPoint = DirectX::XMVectorSet(0.0f, 100.0f, 0.0f, 1.0f);
    DirectX::XMVECTOR upDirection = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    DirectX::XMMATRIX viewMatrix = DirectX::XMMatrixLookAtLH(eyePosition, focusPoint, upDirection);

    float fovAngleY = DirectX::XMConvertToRadians(45.0f);
    float aspectRatio = (float)g_width / (float)g_height;
    float nearZ = 0.1f;
    float farZ = 5000.0f;

    DirectX::XMMATRIX projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(fovAngleY, aspectRatio, nearZ, farZ);

    App->cb.world = DirectX::XMMatrixTranspose(worldMatrix);
    App->cb.view = DirectX::XMMatrixTranspose(viewMatrix);
    App->cb.proj = DirectX::XMMatrixTranspose(projectionMatrix);
    App->cb.lightDir = { -0.5f, -0.5f, 0.5f };

    // === 骨骼索引循环 ===
    static int lastBoneIndex = -1;
    static std::vector<std::string> boneNames;
    static bool boneNamesInitialized = false;

    // 初始化骨骼名列表（只做一次）
    if (!boneNamesInitialized && !App->boneNameToIndex.empty()) {
        boneNames.resize(App->boneNameToIndex.size());
        for (const auto& kv : App->boneNameToIndex) {
            if (kv.second >= 0 && kv.second < (int)boneNames.size())
                boneNames[kv.second] = kv.first;
        }
        boneNamesInitialized = true;
    }

    int boneCount = (int)boneNames.size();
    int period = 2; // 每2秒切换一次
    int curBoneIndex = 0;
    if (boneCount > 0) {
        curBoneIndex = int(time / period) % boneCount;
        App->cb.targetBoneIndex = curBoneIndex;

        // 只在切换时打印
        if (curBoneIndex != lastBoneIndex) {
            std::cout << "[Bone switch] Current bone: " << boneNames[curBoneIndex] << " (index=" << curBoneIndex << ")" << std::endl;
            lastBoneIndex = curBoneIndex;
        }
    }
    else {
        App->cb.targetBoneIndex = 0;
    }


    g_pImmediateContext->UpdateSubresource(App->constantBuffer, 0, nullptr, &App->cb, 0, 0);
    g_pImmediateContext->VSSetConstantBuffers(0, 1, &App->constantBuffer);
    g_pImmediateContext->PSSetConstantBuffers(0, 1, &App->constantBuffer);

    // 计算动画时间
    float ticksPerSecond = App->animTicksPerSecond > 0 ? App->animTicksPerSecond : 25.0f;
    float animTime = fmod(time * ticksPerSecond, App->animDuration);

    // 递归收集骨骼变换并填充矩阵
    if (App->scene && App->scene->mRootNode) {
        // 清零
        for (int i = 0; i < 128; ++i)
            App->boneMatrixData.boneMatrices[i] = DirectX::XMMatrixIdentity();

        std::map<std::string, aiMatrix4x4> nodeGlobalTransforms; // 可选
        CollectAnimatedBoneMatrices(
            App->scene->mRootNode, aiMatrix4x4(),
            App->boneAnimCache,
            App->boneNameToIndex,
            App->boneOffsetMatrices,
            animTime,
            nodeGlobalTransforms,
            App->boneMatrixData.boneMatrices,
            128
        );

        // 更新到 GPU
        g_pImmediateContext->UpdateSubresource(App->boneMatrixBuffer, 0, nullptr, &App->boneMatrixData, 0, 0);
        // 绑定到 VS 常量缓冲区槽1（假设槽0是普通常量缓冲区）
        g_pImmediateContext->VSSetConstantBuffers(1, 1, &App->boneMatrixBuffer);
    }


    // 更新动画骨骼位置
    std::map<std::string, aiVector3D> bonePositions;
    if (App->scene && App->scene->mRootNode)
        CollectAnimatedBonePositions(App->scene->mRootNode, aiMatrix4x4(), App->boneAnimCache, animTime, bonePositions);
    {
        // 1. 利用动画后的 bonePositions 生成骨骼连线
        std::vector<aiVector3D> boneLines;
        CollectBoneLines(App->scene->mRootNode, bonePositions, boneLines);

        // 2. 重新创建骨骼线顶点缓冲区
        if (!boneLines.empty()) {
            // 先释放旧的
            if (App->boneLineVB) {
                App->boneLineVB->Release();
                App->boneLineVB = nullptr;
            }
            D3D11_BUFFER_DESC bd = {};
            bd.Usage = D3D11_USAGE_DEFAULT;
            bd.ByteWidth = UINT(sizeof(aiVector3D) * boneLines.size());
            bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA initData = {};
            initData.pSysMem = boneLines.data();
            ID3D11Buffer* boneLineVB = nullptr;
            g_pd3dDevice->CreateBuffer(&bd, &initData, &boneLineVB);

            App->boneLineVB = boneLineVB;
            App->boneLineVertexCount = boneLines.size();
        }
    }

    // 构建一个面向相机的 XY 平面小矩形
    {
        if (App->ikQuadVB) {
            App->ikQuadVB->Release();
            App->ikQuadVB = nullptr;
        }
        float halfSize = 2.0f; // 控制立方体半边长度
        aiVector3D c = IK_Position;

        // 构建 8 个顶点（立方体角）
        aiVector3D p[8] = {
            { c.x - halfSize, c.y - halfSize, c.z - halfSize },
            { c.x - halfSize, c.y + halfSize, c.z - halfSize },
            { c.x + halfSize, c.y + halfSize, c.z - halfSize },
            { c.x + halfSize, c.y - halfSize, c.z - halfSize },

            { c.x - halfSize, c.y - halfSize, c.z + halfSize },
            { c.x - halfSize, c.y + halfSize, c.z + halfSize },
            { c.x + halfSize, c.y + halfSize, c.z + halfSize },
            { c.x + halfSize, c.y - halfSize, c.z + halfSize },
        };

        // 构建 12 个三角形，共 36 个顶点（每个面两个三角形）
        aiVector3D cubeVerts[36] = {
            // 前面 z+
            p[4], p[5], p[6],
            p[4], p[6], p[7],
            // 后面 z-
            p[0], p[1], p[2],
            p[0], p[2], p[3],
            // 左面 x-
            p[0], p[4], p[5],
            p[0], p[5], p[1],
            // 右面 x+
            p[3], p[2], p[6],
            p[3], p[6], p[7],
            // 上面 y+
            p[1], p[5], p[6],
            p[1], p[6], p[2],
            // 下面 y-
            p[0], p[3], p[7],
            p[0], p[7], p[4],
        };

        D3D11_BUFFER_DESC bd = {};
        bd.Usage = D3D11_USAGE_DEFAULT;
        bd.ByteWidth = sizeof(cubeVerts);
        bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = cubeVerts;

        g_pd3dDevice->CreateBuffer(&bd, &initData, &App->ikQuadVB);
    }
}


void CreateRasterizerStates()
{
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_NONE; // ❗ 关闭剔除
    rsDesc.FrontCounterClockwise = FALSE;
    rsDesc.DepthClipEnable = TRUE;

    g_pd3dDevice->CreateRasterizerState(&rsDesc, &g_pRasterState_NoCull);
}


int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    std::unique_ptr<App> app_inst = std::make_unique<App>();


    if (FAILED(InitWindow(hInstance, nCmdShow)))
        return 0;

    if (FAILED(InitD3D()))
    {
        Cleanup();
        return 0;
    }

    CreateRasterizerStates();

    if (FAILED(InitShaders(app_inst.get())))
    {
		Cleanup();
		return 0;
	}

    if (!InitBoneLineShader(app_inst.get())) {
        Cleanup();
        return 0;
    }

    if (!LoadModel("data/Taunt.fbx", app_inst.get()))
    {
        Cleanup();
        return 0;
    }

    int ret = Run(app_inst.get());

    Cleanup();

    return ret;
}