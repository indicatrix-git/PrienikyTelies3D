/* ======================================================================
 *  Prienik dvoch kruhovych valcov - DirectX 12, jazyk C (nie C++)
 * ----------------------------------------------------------------------
 *  Port povodneho Python/C# programu do DX12. Cielom je co najjednoduchsi
 *  funkcny kod (nie optimalizovany), ktory bezi na Windows 11 aj na
 *  zakladnej grafike (v pripade nutnosti pouzije softverovy WARP adapter).
 *
 *  Co program robi:
 *    - nacita parametre oboch valcov z textoveho suboru (default "valce.txt"
 *      alebo prvy argument prikazoveho riadku),
 *    - vygeneruje polygonalnu siet oboch valcov (plosky + wireframe),
 *    - numericky najde prienikovu krivku (kvadraticka rovnica priamka x valec)
 *      a vytvori z nej hrubu cervenu rurku,
 *    - vykresli scenu: valec A sivy PLNY, valec B sivy PRIEHLADNY,
 *      biely wireframe, biele osi, cerveny prienik na ciernom pozadi.
 *
 *  Ovladanie:
 *    sipky    - otacanie kamery (hore/dole = sklon, vlavo/vpravo = azimut)
 *    mys (LMB)- tahanim otacanie kamery
 *    koliesko - priblizenie / oddialenie
 *    + / -    - priblizenie / oddialenie
 *    Esc      - koniec
 *
 *  Preklad (Developer Command Prompt for VS):
 *    cl /Fe:prienik.exe prienik.c
 *  (potrebne kniznice su pripojene cez #pragma comment nizsie)
 *
 *  Spustenie:
 *    prienik.exe            (nacita valce.txt z aktualneho priecinka)
 *    prienik.exe moje.txt   (nacita zadany subor)
 * ====================================================================== */

#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
/* aby sa pouzil WinMain ako vstupny bod (GUI aplikacia bez konzoly) */
#pragma comment(linker, "/subsystem:windows")

/* ---------------------------------------------------------------------- */
/*  Pomocna kontrola HRESULT                                              */
/* ---------------------------------------------------------------------- */
static void Die(const char *msg)
{
    MessageBoxA(NULL, msg, "Chyba", MB_OK | MB_ICONERROR);
    exit(1);
}
#define HR(x, m) do { if (FAILED(x)) Die(m); } while (0)

/* ====================================================================== */
/*  Jednoduca 3D matematika                                               */
/* ====================================================================== */
typedef struct { float x, y, z; } V3;

static V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static V3 v_add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static V3 v_sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static V3 v_mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float v_dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static V3 v_cross(V3 a, V3 b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static float v_len(V3 a) { return (float)sqrt(v_dot(a, a)); }
static V3 v_norm(V3 a) { float l = v_len(a); return (l < 1e-9f) ? a : v_mul(a, 1.0f / l); }

/* matica 4x4 - row-major, konvencia riadkoveho vektora: v' = v * M */
typedef struct { float m[16]; } Mat;

static Mat mat_mul(Mat a, Mat b)
{
    Mat r; int i, j, k;
    for (i = 0; i < 4; i++)
        for (j = 0; j < 4; j++) {
            float s = 0.0f;
            for (k = 0; k < 4; k++) s += a.m[i * 4 + k] * b.m[k * 4 + j];
            r.m[i * 4 + j] = s;
        }
    return r;
}

/* lavorucna look-at matica (DirectXMath XMMatrixLookAtLH) */
static Mat mat_lookat_lh(V3 eye, V3 at, V3 up)
{
    V3 zx = v_norm(v_sub(at, eye));
    V3 xx = v_norm(v_cross(up, zx));
    V3 yx = v_cross(zx, xx);
    Mat r;
    r.m[0] = xx.x; r.m[1] = yx.x; r.m[2] = zx.x; r.m[3] = 0;
    r.m[4] = xx.y; r.m[5] = yx.y; r.m[6] = zx.y; r.m[7] = 0;
    r.m[8] = xx.z; r.m[9] = yx.z; r.m[10] = zx.z; r.m[11] = 0;
    r.m[12] = -v_dot(xx, eye);
    r.m[13] = -v_dot(yx, eye);
    r.m[14] = -v_dot(zx, eye);
    r.m[15] = 1;
    return r;
}

/* lavorucna perspektiva, hlbka v rozsahu [0,1] (XMMatrixPerspectiveFovLH) */
static Mat mat_persp_lh(float fovy, float aspect, float zn, float zf)
{
    float ys = 1.0f / (float)tan(fovy * 0.5f);
    float xs = ys / aspect;
    Mat r; memset(&r, 0, sizeof(r));
    r.m[0] = xs;
    r.m[5] = ys;
    r.m[10] = zf / (zf - zn);
    r.m[11] = 1.0f;
    r.m[14] = -zn * zf / (zf - zn);
    return r;
}

/* ====================================================================== */
/*  Parametre sceny (nacitane zo suboru)                                  */
/* ====================================================================== */
typedef struct {
    double rA, lA, angA; int sidesA, segA;
    double rB, lB, angB; int sidesB, segB;
    int    iSamples, iSides; double iRadius;
    double bAlpha;
} Scene;

static void scene_defaults(Scene *s)
{
    s->rA = 2.5; s->lA = 12; s->angA = 45;  s->sidesA = 24; s->segA = 1;
    s->rB = 1.5; s->lB = 12; s->angB = -45; s->sidesB = 24; s->segB = 1;
    s->iSamples = 360; s->iSides = 8; s->iRadius = 0.07;
    s->bAlpha = 0.40;
}

static void scene_load(Scene *s, const char *path)
{
    FILE *f = fopen(path, "r");
    char line[256], key[64];
    double val;
    if (!f) { /* subor neexistuje -> zostanu predvolene hodnoty */ return; }
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (sscanf(line, "%63s %lf", key, &val) != 2) continue;
        if      (!strcmp(key, "A_radius"))   s->rA = val;
        else if (!strcmp(key, "A_length"))   s->lA = val;
        else if (!strcmp(key, "A_angle"))    s->angA = val;
        else if (!strcmp(key, "A_sides"))    s->sidesA = (int)val;
        else if (!strcmp(key, "A_segments")) s->segA = (int)val;
        else if (!strcmp(key, "B_radius"))   s->rB = val;
        else if (!strcmp(key, "B_length"))   s->lB = val;
        else if (!strcmp(key, "B_angle"))    s->angB = val;
        else if (!strcmp(key, "B_sides"))    s->sidesB = (int)val;
        else if (!strcmp(key, "B_segments")) s->segB = (int)val;
        else if (!strcmp(key, "I_samples"))  s->iSamples = (int)val;
        else if (!strcmp(key, "I_radius"))   s->iRadius = val;
        else if (!strcmp(key, "I_sides"))    s->iSides = (int)val;
        else if (!strcmp(key, "B_alpha"))    s->bAlpha = val;
    }
    fclose(f);
    if (s->sidesA < 3) s->sidesA = 3;
    if (s->sidesB < 3) s->sidesB = 3;
    if (s->segA < 1) s->segA = 1;
    if (s->segB < 1) s->segB = 1;
    if (s->iSamples < 16) s->iSamples = 16;
    if (s->iSides < 3) s->iSides = 3;
}

/* ====================================================================== */
/*  Generovanie geometrie do polí (vrcholy = pozicia float3)              */
/* ====================================================================== */
typedef struct {
    float    *verts;   /* x,y,z, x,y,z, ...        */
    int       vcount;  /* pocet vrcholov           */
    int       vcap;    /* alokovana kapacita vrcholov */
    uint32_t *idx;     /* indexy                   */
    int       icount;  /* pocet indexov            */
    int       icap;    /* alokovana kapacita indexov  */
} MeshData;

/* dynamicke pridavanie (kapacita drzana v strukture -> bezpecne aj pri
   opakovanom pridavani do tej istej siete) */
static void md_init(MeshData *m) { memset(m, 0, sizeof(*m)); }
static int  md_addv(MeshData *m, V3 p) {
    if (m->vcount + 1 > m->vcap) {
        m->vcap = (m->vcap ? m->vcap * 2 : 256);
        m->verts = (float *)realloc(m->verts, (size_t)m->vcap * 3 * sizeof(float));
    }
    m->verts[m->vcount * 3 + 0] = p.x;
    m->verts[m->vcount * 3 + 1] = p.y;
    m->verts[m->vcount * 3 + 2] = p.z;
    return m->vcount++;
}
static void md_addi(MeshData *m, uint32_t a) {
    if (m->icount + 1 > m->icap) {
        m->icap = (m->icap ? m->icap * 2 : 256);
        m->idx = (uint32_t *)realloc(m->idx, (size_t)m->icap * sizeof(uint32_t));
    }
    m->idx[m->icount++] = a;
}

/* ortonormalny ram kolmy na danu os */
static void frame_of(V3 axis, V3 *u1, V3 *u2)
{
    V3 z = v3(0, 0, 1);
    V3 t = v_cross(axis, z);
    if (v_len(t) < 1e-6f) t = v_cross(axis, v3(0, 1, 0));
    *u1 = v_norm(t);
    *u2 = v_norm(v_cross(axis, *u1));
}

/* plast + podstavy valca (trojuholniky) */
static void gen_cylinder(MeshData *m, V3 axis, double r, double L, int sides, int segs)
{
    V3 u1, u2; frame_of(axis, &u1, &u2);
    int rings = segs + 1;
    double half = L * 0.5;
    int i, j;
    int base = m->vcount;
    /* plast: rings x sides vrcholov */
    for (i = 0; i < rings; i++) {
        double t = -half + L * i / (double)segs;
        for (j = 0; j < sides; j++) {
            double th = 2.0 * 3.14159265358979 * j / sides;
            V3 radial = v_add(v_mul(u1, (float)cos(th)), v_mul(u2, (float)sin(th)));
            V3 p = v_add(v_mul(axis, (float)t), v_mul(radial, (float)r));
            md_addv(m, p);
        }
    }
    for (i = 0; i < segs; i++) {
        for (j = 0; j < sides; j++) {
            uint32_t a = base + i * sides + j;
            uint32_t b = base + i * sides + (j + 1) % sides;
            uint32_t c = base + (i + 1) * sides + (j + 1) % sides;
            uint32_t d = base + (i + 1) * sides + j;
            md_addi(m, a); md_addi(m, b); md_addi(m, c);
            md_addi(m, a); md_addi(m, c); md_addi(m, d);
        }
    }
    /* podstavy (vejare) - aby valec posobil plny */
    int cBot = md_addv(m, v_mul(axis, (float)(-half)));
    int cTop = md_addv(m, v_mul(axis, (float)(half)));
    int ring0 = base;                  /* prvy prstenec (i=0)       */
    int ringL = base + segs * sides;   /* posledny prstenec (i=segs)*/
    for (j = 0; j < sides; j++) {
        uint32_t a = ring0 + j, b = ring0 + (j + 1) % sides;
        md_addi(m, cBot); md_addi(m, b); md_addi(m, a);
        uint32_t c = ringL + j, d = ringL + (j + 1) % sides;
        md_addi(m, cTop); md_addi(m, c); md_addi(m, d);
    }
}

/* prienikova krivka: jedna vetva (znamienko sgn = +1 alebo -1) */
static int branch_points(V3 *out, int samples,
                         V3 aAxis, double rA, V3 bAxis, double rB, int sgn)
{
    /* parametrizujeme kruznicu mensieho valca (os bAxis), priesecnik
       tvoriacej priamky s vacsim valcom (os aAxis) -> kvadraticka rovnica */
    V3 w1, w2; frame_of(bAxis, &w1, &w2);
    double adotb = v_dot(aAxis, bAxis);
    int n = 0, k;
    for (k = 0; k < samples; k++) {
        double phi = 2.0 * 3.14159265358979 * k / samples;
        V3 c = v_mul(v_add(v_mul(w1, (float)cos(phi)), v_mul(w2, (float)sin(phi))), (float)rB);
        double ac = v_dot(aAxis, c);
        double A2 = 1.0 - adotb * adotb;
        double B1 = -2.0 * adotb * ac;
        double dist2 = v_dot(c, c) - ac * ac;
        double C0 = dist2 - rA * rA;
        double D = B1 * B1 - 4.0 * A2 * C0;
        if (D < 0.0 || fabs(A2) < 1e-12) { out[n++] = c; continue; }
        double t = (-B1 + sgn * sqrt(D)) / (2.0 * A2);
        out[n++] = v_add(c, v_mul(bAxis, (float)t));
    }
    return n;
}

/* z polyciary (uzavretej slucky) vytvori rurku (trojuholniky) */
static void tube_from_polyline(MeshData *m, V3 *pts, int n, double rad, int sides)
{
    int base = m->vcount;
    int k, j;
    for (k = 0; k < n; k++) {
        V3 prev = pts[(k - 1 + n) % n];
        V3 next = pts[(k + 1) % n];
        V3 tang = v_norm(v_sub(next, prev));
        V3 n1, n2;
        V3 up = v3(0, 0, 1);
        n1 = v_cross(tang, up);
        if (v_len(n1) < 1e-5f) n1 = v_cross(tang, v3(1, 0, 0));
        n1 = v_norm(n1);
        n2 = v_norm(v_cross(tang, n1));
        for (j = 0; j < sides; j++) {
            double a = 2.0 * 3.14159265358979 * j / sides;
            V3 off = v_add(v_mul(n1, (float)cos(a)), v_mul(n2, (float)sin(a)));
            md_addv(m, v_add(pts[k], v_mul(off, (float)rad)));
        }
    }
    for (k = 0; k < n; k++) {
        int k2 = (k + 1) % n;
        for (j = 0; j < sides; j++) {
            int j2 = (j + 1) % sides;
            uint32_t a = base + k * sides + j;
            uint32_t b = base + k * sides + j2;
            uint32_t c = base + k2 * sides + j2;
            uint32_t d = base + k2 * sides + j;
            md_addi(m, a); md_addi(m, b); md_addi(m, c);
            md_addi(m, a); md_addi(m, c); md_addi(m, d);
        }
    }
}

/* ====================================================================== */
/*  DirectX 12 globalny stav                                              */
/* ====================================================================== */
#define FRAMES 2

static HWND                       g_hwnd;
static ID3D12Device              *g_dev;
static ID3D12CommandQueue        *g_queue;
static IDXGISwapChain3           *g_swap;
static ID3D12DescriptorHeap      *g_rtvHeap, *g_dsvHeap;
static UINT                       g_rtvSize;
static ID3D12Resource            *g_rtv[FRAMES];
static ID3D12Resource            *g_depth;
static ID3D12CommandAllocator    *g_alloc;
static ID3D12GraphicsCommandList *g_cmd;
static ID3D12RootSignature       *g_rootSig;
static ID3D12PipelineState       *g_psoSolid, *g_psoBlend, *g_psoWire, *g_psoLine;
static ID3D12Fence               *g_fence;
static UINT64                     g_fenceVal;
static HANDLE                     g_fenceEvt;

static int   g_w = 1280, g_h = 800;

/* GPU buffery jednej siete */
typedef struct {
    ID3D12Resource *vb; D3D12_VERTEX_BUFFER_VIEW vbv;
    ID3D12Resource *ib; D3D12_INDEX_BUFFER_VIEW  ibv;
    UINT icount;
} GpuMesh;

static GpuMesh g_cylA, g_cylB, g_inter, g_axes;
static float   g_sceneR = 8.0f;
static float   g_bAlpha = 0.4f;

/* kamera */
static float g_yaw = -0.05236f;  /* -3 stupne */
static float g_pitch = 0.38397f; /* 22 stupnov */
static float g_dist = 24.0f;
static int   g_drag = 0; static int g_lastX, g_lastY;

/* ---------------------------------------------------------------------- */
/*  Vytvorenie upload-buffera a nahratie dat                              */
/* ---------------------------------------------------------------------- */
static ID3D12Resource *make_upload_buffer(const void *data, size_t bytes)
{
    D3D12_HEAP_PROPERTIES hp; memset(&hp, 0, sizeof(hp));
    hp.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC rd; memset(&rd, 0, sizeof(rd));
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_UNKNOWN;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    ID3D12Resource *res = NULL;
    HR(ID3D12Device_CreateCommittedResource(g_dev, &hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void **)&res),
        "CreateCommittedResource (buffer)");

    void *p = NULL;
    D3D12_RANGE rr; rr.Begin = 0; rr.End = 0;
    HR(ID3D12Resource_Map(res, 0, &rr, &p), "Map buffer");
    memcpy(p, data, bytes);
    ID3D12Resource_Unmap(res, 0, NULL);
    return res;
}

static GpuMesh upload_mesh(MeshData *md)
{
    GpuMesh g; memset(&g, 0, sizeof(g));
    size_t vbytes = (size_t)md->vcount * 3 * sizeof(float);
    size_t ibytes = (size_t)md->icount * sizeof(uint32_t);
    g.vb = make_upload_buffer(md->verts, vbytes);
    g.ib = make_upload_buffer(md->idx, ibytes);
    g.vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(g.vb);
    g.vbv.SizeInBytes = (UINT)vbytes;
    g.vbv.StrideInBytes = 3 * sizeof(float);
    g.ibv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(g.ib);
    g.ibv.SizeInBytes = (UINT)ibytes;
    g.ibv.Format = DXGI_FORMAT_R32_UINT;
    g.icount = md->icount;
    return g;
}

/* ---------------------------------------------------------------------- */
/*  Synchronizacia - cakanie na dokoncenie GPU (jednoduche)              */
/* ---------------------------------------------------------------------- */
static void wait_gpu(void)
{
    UINT64 v = ++g_fenceVal;
    HR(ID3D12CommandQueue_Signal(g_queue, g_fence, v), "Signal");
    if (ID3D12Fence_GetCompletedValue(g_fence) < v) {
        HR(ID3D12Fence_SetEventOnCompletion(g_fence, v, g_fenceEvt), "SetEvent");
        WaitForSingleObject(g_fenceEvt, INFINITE);
    }
}

/* ---------------------------------------------------------------------- */
/*  Kompilacia shaderov a vytvorenie PSO                                  */
/* ---------------------------------------------------------------------- */
static const char *g_hlsl =
"cbuffer C : register(b0){ row_major float4x4 mvp; float4 col; };\n"
"struct VSOut{ float4 pos:SV_POSITION; };\n"
"VSOut VSMain(float3 p:POSITION){ VSOut o; o.pos = mul(float4(p,1.0), mvp); return o; }\n"
"float4 PSMain(VSOut i):SV_TARGET{ return col; }\n";

static void compile_shaders(ID3DBlob **vs, ID3DBlob **ps)
{
    ID3DBlob *err = NULL;
    if (FAILED(D3DCompile(g_hlsl, strlen(g_hlsl), "shader", NULL, NULL,
        "VSMain", "vs_5_0", 0, 0, vs, &err))) {
        Die(err ? (char *)ID3D10Blob_GetBufferPointer(err) : "VS compile");
    }
    if (FAILED(D3DCompile(g_hlsl, strlen(g_hlsl), "shader", NULL, NULL,
        "PSMain", "ps_5_0", 0, 0, ps, &err))) {
        Die(err ? (char *)ID3D10Blob_GetBufferPointer(err) : "PS compile");
    }
}

/* zostavenie jedneho PSO podla parametrov */
static ID3D12PipelineState *make_pso(ID3DBlob *vs, ID3DBlob *ps,
    D3D12_FILL_MODE fill, D3D12_PRIMITIVE_TOPOLOGY_TYPE topo,
    int blend, int depthWrite)
{
    D3D12_INPUT_ELEMENT_DESC il;
    memset(&il, 0, sizeof(il));
    il.SemanticName = "POSITION";
    il.Format = DXGI_FORMAT_R32G32B32_FLOAT;
    il.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC pd;
    memset(&pd, 0, sizeof(pd));
    pd.pRootSignature = g_rootSig;
    pd.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vs);
    pd.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vs);
    pd.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(ps);
    pd.PS.BytecodeLength = ID3D10Blob_GetBufferSize(ps);
    pd.InputLayout.pInputElementDescs = &il;
    pd.InputLayout.NumElements = 1;
    pd.PrimitiveTopologyType = topo;
    pd.NumRenderTargets = 1;
    pd.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pd.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    pd.SampleDesc.Count = 1;
    pd.SampleMask = 0xffffffff;

    /* rasterizer */
    pd.RasterizerState.FillMode = fill;
    pd.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pd.RasterizerState.DepthClipEnable = TRUE;

    /* depth */
    pd.DepthStencilState.DepthEnable = TRUE;
    pd.DepthStencilState.DepthWriteMask = depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    pd.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

    /* blend */
    pd.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    if (blend) {
        pd.BlendState.RenderTarget[0].BlendEnable = TRUE;
        pd.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
        pd.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
        pd.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
        pd.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
        pd.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    }

    ID3D12PipelineState *pso = NULL;
    HR(ID3D12Device_CreateGraphicsPipelineState(g_dev, &pd,
        &IID_ID3D12PipelineState, (void **)&pso), "CreateGraphicsPipelineState");
    return pso;
}

/* ---------------------------------------------------------------------- */
/*  Inicializacia DX12                                                    */
/* ---------------------------------------------------------------------- */
static void dx_init(void)
{
    UINT facFlags = 0;
#ifdef _DEBUG
    {
        ID3D12Debug *dbg = NULL;
        if (SUCCEEDED(D3D12GetDebugInterface(&IID_ID3D12Debug, (void **)&dbg))) {
            ID3D12Debug_EnableDebugLayer(dbg);
            facFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }
    }
#endif
    IDXGIFactory4 *fac = NULL;
    HR(CreateDXGIFactory2(facFlags, &IID_IDXGIFactory4, (void **)&fac), "CreateDXGIFactory2");

    /* skus hardverovy adapter, inak softverovy WARP (zakladna grafika) */
    if (FAILED(D3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&g_dev))) {
        IDXGIAdapter *warp = NULL;
        HR(IDXGIFactory4_EnumWarpAdapter(fac, &IID_IDXGIAdapter, (void **)&warp), "EnumWarpAdapter");
        HR(D3D12CreateDevice((IUnknown *)warp, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void **)&g_dev),
            "D3D12CreateDevice WARP");
    }

    /* command queue */
    D3D12_COMMAND_QUEUE_DESC qd; memset(&qd, 0, sizeof(qd));
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    HR(ID3D12Device_CreateCommandQueue(g_dev, &qd, &IID_ID3D12CommandQueue, (void **)&g_queue),
        "CreateCommandQueue");

    /* swap chain */
    DXGI_SWAP_CHAIN_DESC1 sd; memset(&sd, 0, sizeof(sd));
    sd.BufferCount = FRAMES;
    sd.Width = g_w; sd.Height = g_h;
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.SampleDesc.Count = 1;
    IDXGISwapChain1 *sc1 = NULL;
    HR(IDXGIFactory4_CreateSwapChainForHwnd(fac, (IUnknown *)g_queue, g_hwnd, &sd, NULL, NULL, &sc1),
        "CreateSwapChainForHwnd");
    HR(IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void **)&g_swap), "QI SwapChain3");

    /* RTV heap */
    D3D12_DESCRIPTOR_HEAP_DESC hd; memset(&hd, 0, sizeof(hd));
    hd.NumDescriptors = FRAMES;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    HR(ID3D12Device_CreateDescriptorHeap(g_dev, &hd, &IID_ID3D12DescriptorHeap, (void **)&g_rtvHeap),
        "RTV heap");
    g_rtvSize = ID3D12Device_GetDescriptorHandleIncrementSize(g_dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_rtvHeap, &rtv);
    for (UINT i = 0; i < FRAMES; i++) {
        HR(IDXGISwapChain3_GetBuffer(g_swap, i, &IID_ID3D12Resource, (void **)&g_rtv[i]), "GetBuffer");
        ID3D12Device_CreateRenderTargetView(g_dev, g_rtv[i], NULL, rtv);
        rtv.ptr += g_rtvSize;
    }

    /* DSV heap + depth buffer */
    D3D12_DESCRIPTOR_HEAP_DESC dd; memset(&dd, 0, sizeof(dd));
    dd.NumDescriptors = 1;
    dd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    HR(ID3D12Device_CreateDescriptorHeap(g_dev, &dd, &IID_ID3D12DescriptorHeap, (void **)&g_dsvHeap),
        "DSV heap");

    D3D12_HEAP_PROPERTIES hp; memset(&hp, 0, sizeof(hp));
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC td; memset(&td, 0, sizeof(td));
    td.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    td.Width = g_w; td.Height = g_h; td.DepthOrArraySize = 1; td.MipLevels = 1;
    td.Format = DXGI_FORMAT_D32_FLOAT; td.SampleDesc.Count = 1;
    td.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE cv; memset(&cv, 0, sizeof(cv));
    cv.Format = DXGI_FORMAT_D32_FLOAT; cv.DepthStencil.Depth = 1.0f;
    HR(ID3D12Device_CreateCommittedResource(g_dev, &hp, D3D12_HEAP_FLAG_NONE, &td,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, &IID_ID3D12Resource, (void **)&g_depth),
        "Depth resource");
    D3D12_CPU_DESCRIPTOR_HANDLE dsv;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_dsvHeap, &dsv);
    D3D12_DEPTH_STENCIL_VIEW_DESC dvd; memset(&dvd, 0, sizeof(dvd));
    dvd.Format = DXGI_FORMAT_D32_FLOAT;
    dvd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateDepthStencilView(g_dev, g_depth, &dvd, dsv);

    /* command allocator + list */
    HR(ID3D12Device_CreateCommandAllocator(g_dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void **)&g_alloc), "CreateCommandAllocator");
    HR(ID3D12Device_CreateCommandList(g_dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_alloc, NULL,
        &IID_ID3D12GraphicsCommandList, (void **)&g_cmd), "CreateCommandList");
    ID3D12GraphicsCommandList_Close(g_cmd);

    /* root signature: 20 x 32-bit konstant (16 = MVP, 4 = farba) */
    D3D12_ROOT_PARAMETER rp; memset(&rp, 0, sizeof(rp));
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rp.Constants.ShaderRegister = 0;
    rp.Constants.Num32BitValues = 20;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd; memset(&rsd, 0, sizeof(rsd));
    rsd.NumParameters = 1; rsd.pParameters = &rp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob *rsBlob = NULL, *rsErr = NULL;
    HR(D3D12SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr),
        "SerializeRootSignature");
    HR(ID3D12Device_CreateRootSignature(g_dev, 0,
        ID3D10Blob_GetBufferPointer(rsBlob), ID3D10Blob_GetBufferSize(rsBlob),
        &IID_ID3D12RootSignature, (void **)&g_rootSig), "CreateRootSignature");

    /* PSO */
    ID3DBlob *vs = NULL, *ps = NULL; compile_shaders(&vs, &ps);
    g_psoSolid = make_pso(vs, ps, D3D12_FILL_MODE_SOLID, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 0, 1);
    g_psoBlend = make_pso(vs, ps, D3D12_FILL_MODE_SOLID, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 1, 0);
    g_psoWire  = make_pso(vs, ps, D3D12_FILL_MODE_WIREFRAME, D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE, 0, 1);
    g_psoLine  = make_pso(vs, ps, D3D12_FILL_MODE_SOLID, D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE, 0, 1);

    HR(ID3D12Device_CreateFence(g_dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void **)&g_fence),
        "CreateFence");
    g_fenceVal = 0;
    g_fenceEvt = CreateEvent(NULL, FALSE, FALSE, NULL);
}

/* ---------------------------------------------------------------------- */
/*  Vybudovanie celej geometrie sceny                                     */
/* ---------------------------------------------------------------------- */
static void build_scene(Scene *s)
{
    double degA = s->angA * 3.14159265358979 / 180.0;
    double degB = s->angB * 3.14159265358979 / 180.0;
    V3 axA = v3((float)cos(degA), (float)sin(degA), 0);
    V3 axB = v3((float)cos(degB), (float)sin(degB), 0);
    g_bAlpha = (float)s->bAlpha;

    /* valec A */
    MeshData mA; md_init(&mA);
    gen_cylinder(&mA, axA, s->rA, s->lA, s->sidesA, s->segA);
    g_cylA = upload_mesh(&mA);

    /* valec B */
    MeshData mB; md_init(&mB);
    gen_cylinder(&mB, axB, s->rB, s->lB, s->sidesB, s->segB);
    g_cylB = upload_mesh(&mB);

    /* prienik - krivka lezi na MENSOM valci */
    V3 carrierAx, otherAx; double rCarrier, rOther;
    if (s->rB <= s->rA) { carrierAx = axB; rCarrier = s->rB; otherAx = axA; rOther = s->rA; }
    else                { carrierAx = axA; rCarrier = s->rA; otherAx = axB; rOther = s->rB; }

    V3 *bp = (V3 *)malloc(sizeof(V3) * s->iSamples);
    MeshData mI; md_init(&mI);
    int n1 = branch_points(bp, s->iSamples, otherAx, rOther, carrierAx, rCarrier, +1);
    tube_from_polyline(&mI, bp, n1, s->iRadius, s->iSides);
    int n2 = branch_points(bp, s->iSamples, otherAx, rOther, carrierAx, rCarrier, -1);
    tube_from_polyline(&mI, bp, n2, s->iRadius, s->iSides);
    free(bp);
    g_inter = upload_mesh(&mI);

    /* mierka sceny */
    double maxL = (s->lA > s->lB ? s->lA : s->lB);
    double maxR = (s->rA > s->rB ? s->rA : s->rB);
    g_sceneR = (float)(maxL * 0.5 + maxR + 0.5);

    /* suradnicove osi (line list): -os .. +os pre X,Y,Z */
    float L = g_sceneR;
    float axv[] = {
        -L, 0, 0,  L, 0, 0,
         0,-L, 0,  0, L, 0,
         0, 0,-L*0.6f, 0, 0, L*0.6f
    };
    uint32_t axi[] = { 0,1, 2,3, 4,5 };
    MeshData mX; md_init(&mX);
    {
    int i;
    for (i = 0; i < 6; i++)
        md_addv(&mX, v3(axv[i*3], axv[i*3+1], axv[i*3+2]));

    for (i = 0; i < 6; i++)
        md_addi(&mX, axi[i]);
    }
    g_axes = upload_mesh(&mX);

    free(mA.verts); free(mA.idx);
    free(mB.verts); free(mB.idx);
    free(mI.verts); free(mI.idx);
    free(mX.verts); free(mX.idx);
}

/* ---------------------------------------------------------------------- */
/*  Nastavenie root konstant (MVP + farba) a vykreslenie siete            */
/* ---------------------------------------------------------------------- */
static void draw_mesh(GpuMesh *g, Mat mvp, float r, float gg, float b, float a,
                      D3D12_PRIMITIVE_TOPOLOGY topo)
{
    float cb[20];
    memcpy(cb, mvp.m, 16 * sizeof(float));
    cb[16] = r; cb[17] = gg; cb[18] = b; cb[19] = a;
    ID3D12GraphicsCommandList_SetGraphicsRoot32BitConstants(g_cmd, 0, 20, cb, 0);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(g_cmd, topo);
    ID3D12GraphicsCommandList_IASetVertexBuffers(g_cmd, 0, 1, &g->vbv);
    ID3D12GraphicsCommandList_IASetIndexBuffer(g_cmd, &g->ibv);
    ID3D12GraphicsCommandList_DrawIndexedInstanced(g_cmd, g->icount, 1, 0, 0, 0);
}

/* ---------------------------------------------------------------------- */
/*  Vykreslenie jedneho snimku                                            */
/* ---------------------------------------------------------------------- */
static void render(void)
{
    /* MVP z kamery */
    if (g_pitch > 1.5f) g_pitch = 1.5f;
    if (g_pitch < -1.5f) g_pitch = -1.5f;
    if (g_dist < 2.0f) g_dist = 2.0f;
    V3 dir = v3((float)(cos(g_pitch) * cos(g_yaw)),
                (float)(cos(g_pitch) * sin(g_yaw)),
                (float)sin(g_pitch));
    V3 eye = v_mul(dir, g_dist);
    Mat view = mat_lookat_lh(eye, v3(0, 0, 0), v3(0, 0, 1));
    Mat proj = mat_persp_lh(3.14159265f / 4.0f, (float)g_w / g_h, 0.1f, 500.0f);
    Mat mvp = mat_mul(view, proj);

    UINT idx = IDXGISwapChain3_GetCurrentBackBufferIndex(g_swap);

    HR(ID3D12CommandAllocator_Reset(g_alloc), "alloc reset");
    HR(ID3D12GraphicsCommandList_Reset(g_cmd, g_alloc, NULL), "cmd reset");

    /* PRESENT -> RENDER_TARGET */
    D3D12_RESOURCE_BARRIER bar; memset(&bar, 0, sizeof(bar));
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = g_rtv[idx];
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(g_cmd, 1, &bar);

    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_rtvHeap, &rtv);
    rtv.ptr += (SIZE_T)idx * g_rtvSize;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv;
    ID3D12DescriptorHeap_GetCPUDescriptorHandleForHeapStart(g_dsvHeap, &dsv);

    ID3D12GraphicsCommandList_OMSetRenderTargets(g_cmd, 1, &rtv, FALSE, &dsv);

    float black[4] = { 0, 0, 0, 1 };
    ID3D12GraphicsCommandList_ClearRenderTargetView(g_cmd, rtv, black, 0, NULL);
    ID3D12GraphicsCommandList_ClearDepthStencilView(g_cmd, dsv,
        D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

    D3D12_VIEWPORT vp; vp.TopLeftX = 0; vp.TopLeftY = 0;
    vp.Width = (float)g_w; vp.Height = (float)g_h; vp.MinDepth = 0; vp.MaxDepth = 1;
    D3D12_RECT sc; sc.left = 0; sc.top = 0; sc.right = g_w; sc.bottom = g_h;
    ID3D12GraphicsCommandList_RSSetViewports(g_cmd, 1, &vp);
    ID3D12GraphicsCommandList_RSSetScissorRects(g_cmd, 1, &sc);

    ID3D12GraphicsCommandList_SetGraphicsRootSignature(g_cmd, g_rootSig);

    /* --- 1) osi (biele ciary) --- */
    ID3D12GraphicsCommandList_SetPipelineState(g_cmd, g_psoLine);
    draw_mesh(&g_axes, mvp, 1, 1, 1, 1, D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    /* --- 2) valec A: plne sive plosky (nepriehladny) --- */
    ID3D12GraphicsCommandList_SetPipelineState(g_cmd, g_psoSolid);
    draw_mesh(&g_cylA, mvp, 0.5f, 0.5f, 0.5f, 1.0f, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* --- 3) prienik: cervena rurka (nepriehladna, na povrchu) --- */
    draw_mesh(&g_inter, mvp, 1.0f, 0.0f, 0.0f, 1.0f, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* --- 4) wireframe valca A (biely) --- */
    ID3D12GraphicsCommandList_SetPipelineState(g_cmd, g_psoWire);
    draw_mesh(&g_cylA, mvp, 1, 1, 1, 1, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* --- 5) valec B: ciastocne priehladne sive plosky --- */
    ID3D12GraphicsCommandList_SetPipelineState(g_cmd, g_psoBlend);
    draw_mesh(&g_cylB, mvp, 0.5f, 0.5f, 0.5f, g_bAlpha, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* --- 6) wireframe valca B (biely) --- */
    ID3D12GraphicsCommandList_SetPipelineState(g_cmd, g_psoWire);
    draw_mesh(&g_cylB, mvp, 1, 1, 1, 1, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* RENDER_TARGET -> PRESENT */
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    ID3D12GraphicsCommandList_ResourceBarrier(g_cmd, 1, &bar);

    HR(ID3D12GraphicsCommandList_Close(g_cmd), "cmd close");
    ID3D12CommandList *lists[1]; lists[0] = (ID3D12CommandList *)g_cmd;
    ID3D12CommandQueue_ExecuteCommandLists(g_queue, 1, lists);

    HR(IDXGISwapChain3_Present(g_swap, 1, 0), "Present");
    wait_gpu();
}

/* ---------------------------------------------------------------------- */
/*  Okno (Win32)                                                          */
/* ---------------------------------------------------------------------- */
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_KEYDOWN:
        switch (wp) {
        case VK_LEFT:  g_yaw -= 0.08f; break;
        case VK_RIGHT: g_yaw += 0.08f; break;
        case VK_UP:    g_pitch += 0.08f; break;
        case VK_DOWN:  g_pitch -= 0.08f; break;
        case VK_ADD: case VK_OEM_PLUS:  g_dist *= 0.9f; break;
        case VK_SUBTRACT: case VK_OEM_MINUS: g_dist *= 1.1f; break;
        case VK_ESCAPE: PostQuitMessage(0); break;
        }
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        g_dist *= (d > 0) ? 0.9f : 1.1f;
        return 0;
    }
    case WM_LBUTTONDOWN:
        g_drag = 1; g_lastX = LOWORD(lp); g_lastY = HIWORD(lp); SetCapture(h);
        return 0;
    case WM_LBUTTONUP:
        g_drag = 0; ReleaseCapture();
        return 0;
    case WM_MOUSEMOVE:
        if (g_drag) {
            int x = LOWORD(lp), y = HIWORD(lp);
            g_yaw += (x - g_lastX) * 0.01f;
            g_pitch += (y - g_lastY) * 0.01f;
            g_lastX = x; g_lastY = y;
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR lpCmd, int nShow)
{
    (void)hPrev; (void)nShow;

    /* nacitanie scenara */
    Scene scene; scene_defaults(&scene);
    const char *path = (lpCmd && lpCmd[0]) ? lpCmd : "valce.txt";
    /* lpCmd moze obsahovat uvodzovky/medzery - jednoduche orezanie */
    {
        static char buf[260];
        strncpy(buf, path, sizeof(buf) - 1);
        char *p = buf; while (*p == ' ' || *p == '"') p++;
        char *e = p + strlen(p); while (e > p && (e[-1] == ' ' || e[-1] == '"' || e[-1] == '\n')) *--e = 0;
        scene_load(&scene, p[0] ? p : "valce.txt");
    }

    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "PrienikDX12";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc = { 0, 0, g_w, g_h };
    AdjustWindowRect(&rc, style, FALSE);
    g_hwnd = CreateWindowA("PrienikDX12",
        "Prienik dvoch valcov - DirectX 12 (sipky / mys / koliesko / +- )",
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, hInst, NULL);
    ShowWindow(g_hwnd, SW_SHOW);

    dx_init();
    build_scene(&scene);

    MSG msg; ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT) {
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        } else {
            render();
        }
    }
    wait_gpu();
    return 0;
}
