using BepInEx;
using BepInEx.Logging;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.SceneManagement;

namespace DoronkoWanko_DLSS
{
    [BepInPlugin("com.alex.doronkowankodlss", "DoronkoWanko DLSS", "1.0.2")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;

        void Awake() // Pluginクラスは単なるエントリポイント
        {
            (Log = Logger).LogInfo("Plugin loaded successfully!");
            SceneManager.sceneLoaded += OnSceneLoaded;
        }

        void OnSceneLoaded(Scene scene, LoadSceneMode mode) // シーン遷移の度にカメラ走査
        {
            var dlss = new GameObject("DW_DLSS").AddComponent<DW_DLSS>();
            DontDestroyOnLoad(dlss);
            foreach (var cam in Camera.allCameras)
            {
                Log.LogInfo($"Camera Detected: {scene.name}/{cam.name}({cam.depth})");
                switch (cam.name)
                {
                    case "UI Camera": dlss.uiCamera = cam; break;
                    case "Main Camera": dlss.mainCamera = cam; break;
                }
            }
        }

        public class DW_DLSS : MonoBehaviour
        {
            internal Camera uiCamera;
            internal Camera mainCamera;
            bool uKeyWasDown, rKeyWasDown;
            [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);

            [DllImport("DW_DLSS_N.dll")] static extern void DW_Init();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Update();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Draw();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Release();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Show();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Hide();

            void Awake()
            {
                DW_Init();
            }

            void Update()
            {
                bool uDown = (GetAsyncKeyState(0x55) & 0x8000) != 0;
                if (uDown && !uKeyWasDown) // UIトグル(U) ※タイトル画面だけuiCameraが存在しない
                {
                    if (uiCamera != null)
                        uiCamera.enabled = !uiCamera.enabled;
                }
                uKeyWasDown = uDown;

                bool rDown = (GetAsyncKeyState(0x52) & 0x8000) != 0;
                if (rDown && !rKeyWasDown) // シェーダー(R)
                {
                    // シェーダーリロード
                }
                rKeyWasDown = rDown;
            }

            void LateUpdate()
            {
                DW_Draw(); // 全Update終了後に描画
            }

            void OnDestroy()
            {
                DW_Release();
            }

            void OnApplicationFocus(bool hasFocus)
            {
                if (hasFocus) DW_Show();
                else DW_Hide();
            }



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