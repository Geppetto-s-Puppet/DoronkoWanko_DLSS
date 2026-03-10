using BepInEx;
using BepInEx.Logging;
//using UnityEngine;

namespace DoronkoWanko_DLSS
{
    [BepInPlugin("com.alex.doronkowankodlss", "DoronkoWanko DLSS", "1.0.0")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;

        void Awake()
        {
            Log = Logger;
            Log.LogInfo("DoronkoWanko_DLSS loaded");
        }
    }
}








//namespace PEAK_SSGI
//{


//    [BepInPlugin("com.alex.peakhlsl.plugin", "PEAK HL
//    {
//        internal static ManualLogSource Log { get; private set; } = null!;

//        // ──── ネイティブエクスポート ────
//        [DllImport("com.alex.peakhlsl.native.dll")] static extern void NativeOverlay_Start();
//        [DllImport("com.alex.peakhlsl.native.dll")] static extern void NativeOverlay_Show();
//        [DllImport("com.alex.peakhlsl.native.dll")] static extern void NativeOverlay_Hide();
//        [DllImport("com.alex.peakhlsl.native.dll")] static extern int NativeOverlay_GetStatus();
//        [DllImport("com.alex.peakhlsl.native.dll")] static extern void NativeOverlay_SetMode(int mode);
//        [DllImport("com.alex.peakhlsl.native.dll")] static extern int NativeOverlay_AcquireUnityDevice(IntPtr sampleResourcePtr);
//        [DllImport("com.alex.peakhlsl.native.dll")] static extern void NativeOverlay_UpdateTextures(IntPtr colorPtr, IntPtr depthPtr, IntPtr normalPtr, int screenW, int screenH);

//        // ──── 定数 ────
//        const int VK_SLASH = 0xBF;
//        [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);

//        static readonly string[] ModeNames = { "OFF", "Passthrough", "Depth", "Normals" };

//        // ──── 状態 ────
//        int _shaderMode = 0;   // 0=非表示, 1..3=シェーダー
//        bool _prevDown = false;
//        bool _featureAdded = false;
//        IntPtr _lastColorPtr = IntPtr.Zero;
//        int _lastStatus = -1;

//        RenderTexture? _deviceProbeRT = null;

//        // depthTextureMode をカメラ毎に一度だけ設定する
//        HashSet<Camera> _depthSetCameras = new();

//        // ──── Awake ────
//        void Awake()
//        {
//            Log = Logger;

//            LogRuntimeEnvironment();

//            NativeOverlay_Start();
//            StartCoroutine(AcquireDeviceAfterStart());

//            CapturePass.OnCapture = (col, dep, nor, texW, texH, winW, winH) =>
//            {
//                if (col != _lastColorPtr)
//                {
//                    _lastColorPtr = col;
//                    Log.LogInfo($"[Capture] color={col}  depth={dep}  normal={nor}" +
//                                $"  tex={texW}x{texH}  win={winW}x{winH}");
//                }
//                // unityD3D12Device / unityFence は AcquireUnityDevice で解決済み → 省略
//                NativeOverlay_UpdateTextures(col, dep, nor, winW, winH);
//            };

//            StartCoroutine(EndOfFrameCapture());

//            Log.LogInfo($"Plugin PEAK HLSL is loaded!");
//        }

//        void OnDestroy()
//        {
//            CapturePass.OnCapture = null;
//            if (_deviceProbeRT != null) { _deviceProbeRT.Release(); _deviceProbeRT = null; }
//        }

//        // ──── LateUpdate ────
//        void LateUpdate()
//        {
//            // カメラに深度モードを設定（初回のみ）
//            foreach (var cam in Camera.allCameras)
//            {
//                if (cam.cameraType == CameraType.Game && !_depthSetCameras.Contains(cam))
//                {
//                    cam.depthTextureMode |= DepthTextureMode.Depth | DepthTextureMode.DepthNormals;
//                    _depthSetCameras.Add(cam);
//                    Log.LogInfo($"[LateUpdate] Set depthTextureMode for camera {cam.name}");
//                }
//            }

//            // フレーム5でフィーチャー注入
//            if (!_featureAdded && Time.frameCount == 5)
//            {
//                _featureAdded = true;
//                PatchURPAsset();
//                TryAddFeature();
//                LogRenderInfo();
//            }

//            // ステータスログ（60フレームに1回）
//            if (Time.frameCount % 60 == 0)
//            {
//                int s = NativeOverlay_GetStatus();
//                if (s != _lastStatus) { _lastStatus = s; LogStatus("Status", s); }
//            }

//            // "/" キーでシェーダーモードをサイクル
//            bool down = (GetAsyncKeyState(VK_SLASH) & 0x8000) != 0;
//            if (down && !_prevDown)
//            {
//                _shaderMode = (_shaderMode + 1) % ModeNames.Length;

//                NativeOverlay_SetMode(_shaderMode);

//                if (_shaderMode == 0) NativeOverlay_Hide();
//                else NativeOverlay_Show();

//                Log.LogInfo($"[Shader] mode={_shaderMode} ({ModeNames[_shaderMode]})");
//                LogStatus("NativeStatus", NativeOverlay_GetStatus());
//            }
//            _prevDown = down;
//        }

//        // ──── コルーチン ────
//        IEnumerator EndOfFrameCapture()
//        {
//            while (true)
//            {
//                yield return new WaitForEndOfFrame();
//                CapturePass.ReadAndSend();
//            }
//        }