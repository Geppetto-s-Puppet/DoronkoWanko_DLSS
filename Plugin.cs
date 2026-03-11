using BepInEx;
using BepInEx.Logging;
using System;
using System.Runtime.InteropServices;
using System.Threading;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.SceneManagement;

namespace DoronkoWanko_DLSS
{
    [BepInPlugin("com.alex.doronkowankodlss", "DoronkoWanko DLSS", "1.1.0")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;

        void Awake()
        {
            (Log = Logger).LogInfo("Plugin loaded successfully!");
            SceneManager.sceneLoaded += OnSceneLoaded;
        }

        void OnSceneLoaded(Scene scene, LoadSceneMode mode)
        {
            if (FindObjectOfType<DW_DLSS>() != null) return; // 既にあればスキップ
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

            string shaderPath = null;
            bool isDialogOpen = false;

            [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Init(IntPtr sampleResourcePtr);
            [DllImport("DW_DLSS_N.dll", CharSet = CharSet.Unicode)] static extern void DW_Update(string shaderPath);
            [DllImport("DW_DLSS_N.dll")] static extern void DW_SetTextures(IntPtr[] textures, int count);
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Draw();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Release();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Show();
            [DllImport("DW_DLSS_N.dll")] static extern void DW_Hide();

            [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
            struct OPENFILENAME
            {
                public int lStructSize;
                public IntPtr hwndOwner;
                public IntPtr hInstance;
                public string lpstrFilter;
                public string lpstrCustomFilter;
                public int nMaxCustFilter;
                public int nFilterIndex;
                public string lpstrFile;
                public int nMaxFile;
                public string lpstrFileTitle;
                public int nMaxFileTitle;
                public string lpstrInitialDir;
                public string lpstrTitle;
                public int Flags;
                public short nFileOffset;
                public short nFileExtension;
                public string lpstrDefExt;
                public IntPtr lCustData;
                public IntPtr lpfnHook;
                public string lpTemplateName;
            }
            [DllImport("comdlg32.dll", CharSet = CharSet.Unicode)] static extern bool GetOpenFileName(ref OPENFILENAME ofn);

            void Awake()
            {
                var probeRT = new RenderTexture(4, 4, 0, RenderTextureFormat.ARGB32);
                probeRT.Create();
                DW_Init(probeRT.GetNativeTexturePtr());
                StartCoroutine(InitAndCleanup());

                CapturePass.OnCapture = (colorPtr, depthPtr, normalPtr, motionPtr, shadowPtr, shadowAddPtr, opaquePtr, ssaoPtr, w, h) =>
                {
                    var ptrs = new IntPtr[8] { colorPtr, depthPtr, normalPtr, motionPtr, shadowPtr, shadowAddPtr, opaquePtr, ssaoPtr };
                    DW_SetTextures(ptrs, 8);
                    if (shaderPath != null) DW_Update(shaderPath);
                };
                StartCoroutine(EndOfFrameLoop());
                StartCoroutine(AddFeatureDelayed());
            }

            System.Collections.IEnumerator AddFeatureDelayed()
            {
                yield return null; // 1フレーム待つ
                yield return new WaitForSeconds(12f);
                TryAddFeature();
            }

            System.Collections.IEnumerator InitAndCleanup()
            {
                var probeRT = new RenderTexture(4, 4, 0, RenderTextureFormat.ARGB32);
                probeRT.Create();
                DW_Init(probeRT.GetNativeTexturePtr());
                yield return null; // 1フレーム待ってから解放
                probeRT.Release();
                Destroy(probeRT);
            }



            void Update()
            {
                if (!Application.isFocused) return;
                //if (shaderPath != null) DW_Update(shaderPath);

                bool uDown = (GetAsyncKeyState(0x55) & 0x8000) != 0;
                if (uDown && !uKeyWasDown)
                {
                    if (uiCamera != null)
                        uiCamera.enabled = !uiCamera.enabled;
                }
                uKeyWasDown = uDown;

                bool rDown = (GetAsyncKeyState(0x52) & 0x8000) != 0;
                if (rDown && !rKeyWasDown)
                {
                    DW_Hide();
                    shaderPath = null;
                    isDialogOpen = true;
                    var thread = new System.Threading.Thread(() =>
                    {
                        var ofn = new OPENFILENAME();
                        ofn.lStructSize = Marshal.SizeOf(ofn);
                        ofn.lpstrFile = new string('\0', 256);
                        ofn.nMaxFile = 256;
                        ofn.lpstrFilter = "HLSL Files\0*.hlsl\0";
                        ofn.nFilterIndex = 1;
                        ofn.lpstrInitialDir = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "BepInEx", "plugins");
                        ofn.lpstrTitle = "シェーダーを選択";
                        ofn.Flags = 0x00001000;

                        if (GetOpenFileName(ref ofn))
                        {
                            shaderPath = ofn.lpstrFile;
                            Plugin.Log.LogInfo($"Shader selected: {shaderPath}");
                        }
                        isDialogOpen = false;
                        if (shaderPath != null) DW_Show();
                    });
                    thread.SetApartmentState(System.Threading.ApartmentState.STA);
                    thread.IsBackground = true;
                    thread.Start();
                }
                rKeyWasDown = rDown;
            }

            void LateUpdate()
            {
                if (!isDialogOpen && shaderPath != null) DW_Draw();
            }

            void OnDestroy()
            {
                DW_Release();
            }

            void OnApplicationFocus(bool hasFocus)
            {
                if (isDialogOpen) return;
                if (hasFocus && shaderPath != null) DW_Show();
                else DW_Hide();
            }

            System.Collections.IEnumerator EndOfFrameLoop()
            {
                while (true)
                {
                    yield return new WaitForEndOfFrame();
                    CapturePass.ReadAndSend();
                }
            }

            void TryAddFeature()
            {
                var pl = GraphicsSettings.currentRenderPipeline as UniversalRenderPipelineAsset;
                if (pl == null) { Plugin.Log.LogInfo("TryAddFeature: pl null"); return; }

                var bf = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
                var dataList = pl.GetType().GetField("m_RendererDataList", bf)
                    ?.GetValue(pl) as UnityEngine.Object[];
                if (dataList == null) { Plugin.Log.LogInfo("TryAddFeature: m_RendererDataList null"); return; }

                for (int i = 0; i < dataList.Length; i++)
                {
                    var rd = dataList[i] as ScriptableRendererData;
                    if (rd == null) continue;

                    System.Reflection.FieldInfo fiF = null;
                    System.Reflection.FieldInfo fiM = null;
                    for (var t = rd.GetType(); t != null; t = t.BaseType)
                    {
                        if (fiF == null) fiF = t.GetField("m_RendererFeatures", bf);
                        if (fiM == null) fiM = t.GetField("m_RendererFeatureMap", bf);
                    }
                    if (fiF == null) continue;

                    var features = fiF.GetValue(rd) as System.Collections.Generic.List<ScriptableRendererFeature>;
                    if (features == null) continue;

                    bool exists = false;
                    foreach (var f in features)
                        if (f is CaptureFeature) { exists = true; break; }
                    if (exists) continue;

                    var feat = ScriptableObject.CreateInstance<CaptureFeature>();
                    feat.name = "DW_Capture";
                    DontDestroyOnLoad(feat);
                    feat.Create();
                    features.Add(feat);
                    (fiM?.GetValue(rd) as System.Collections.Generic.List<long>)?.Add(0L);
                    typeof(ScriptableRendererData)
                        .GetMethod("OnValidate", System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance)
                        ?.Invoke(rd, null);
                    Plugin.Log.LogInfo($"CaptureFeature injected into RendererData[{i}] {rd.name}");
                }
            }
        }

        class CapturePass : ScriptableRenderPass
        {
            public static Action<IntPtr, IntPtr, IntPtr, IntPtr, IntPtr, IntPtr, IntPtr, IntPtr, int, int> OnCapture;

            static readonly int s_idColor = Shader.PropertyToID("_DW_Color");
            static readonly int s_idDepth = Shader.PropertyToID("_DW_Depth");
            static readonly int s_idNormal = Shader.PropertyToID("_DW_Normal");
            static readonly int s_idMotion = Shader.PropertyToID("_DW_Motion");
            static readonly int s_idShadow = Shader.PropertyToID("_DW_Shadow");
            static readonly int s_idShadowAdd = Shader.PropertyToID("_DW_ShadowAdd");
            static readonly int s_idOpaque = Shader.PropertyToID("_DW_Opaque");
            static readonly int s_idSSAO = Shader.PropertyToID("_DW_SSAO");

            internal static volatile int s_passExecutedInt = 0;
            internal static volatile int s_lastW = 0;
            internal static volatile int s_lastH = 0;

            public CapturePass()
            {
                renderPassEvent = RenderPassEvent.AfterRenderingPostProcessing;
            }

            public override void Execute(ScriptableRenderContext context, ref RenderingData renderingData)
            {
                var cmd = new CommandBuffer();
                cmd.SetGlobalTexture(s_idColor, renderingData.cameraData.renderer.cameraColorTarget);
                cmd.SetGlobalTexture(s_idDepth, new RenderTargetIdentifier("_CameraDepthTexture"));
                cmd.SetGlobalTexture(s_idNormal, new RenderTargetIdentifier("_CameraNormalsTexture"));
                cmd.SetGlobalTexture(s_idMotion, new RenderTargetIdentifier("_MotionVectorTexture"));
                cmd.SetGlobalTexture(s_idShadow, new RenderTargetIdentifier("_MainLightShadowmapTexture"));
                cmd.SetGlobalTexture(s_idShadowAdd, new RenderTargetIdentifier("_AdditionalLightsShadowmapTexture"));
                cmd.SetGlobalTexture(s_idOpaque, new RenderTargetIdentifier("_CameraOpaqueTexture"));
                cmd.SetGlobalTexture(s_idSSAO, new RenderTargetIdentifier("_ScreenSpaceOcclusionTexture"));
                context.ExecuteCommandBuffer(cmd);
                cmd.Release();

                s_lastW = renderingData.cameraData.camera.pixelWidth;
                s_lastH = renderingData.cameraData.camera.pixelHeight;
                Interlocked.Exchange(ref s_passExecutedInt, 1);
            }

            public static void ReadAndSend()
            {
                if (Interlocked.Exchange(ref s_passExecutedInt, 0) == 0) return;

                IntPtr Get(int id)
                {
                    var t = Shader.GetGlobalTexture(id);
                    return t != null ? t.GetNativeTexturePtr() : IntPtr.Zero;
                }

                IntPtr colorPtr = Get(s_idColor);
                IntPtr depthPtr = Get(s_idDepth);
                IntPtr normalPtr = Get(s_idNormal);
                IntPtr motionPtr = Get(s_idMotion);
                IntPtr shadowPtr = Get(s_idShadow);
                IntPtr shadowAddPtr = Get(s_idShadowAdd);
                IntPtr opaquePtr = Get(s_idOpaque);
                IntPtr ssaoPtr = Get(s_idSSAO);

                Plugin.Log.LogInfo("────────────────────────────────");
                Plugin.Log.LogInfo($"color    = {colorPtr}");
                Plugin.Log.LogInfo($"depth    = {depthPtr}");
                Plugin.Log.LogInfo($"normal   = {normalPtr}");
                Plugin.Log.LogInfo($"motion   = {motionPtr}");
                Plugin.Log.LogInfo($"shadow   = {shadowPtr}");
                Plugin.Log.LogInfo($"shadowAdd= {shadowAddPtr}");
                Plugin.Log.LogInfo($"opaque   = {opaquePtr}");
                Plugin.Log.LogInfo($"ssao     = {ssaoPtr}");
                Plugin.Log.LogInfo($"size     = {s_lastW}x{s_lastH}");
                Plugin.Log.LogInfo("────────────────────────────────");

                OnCapture?.Invoke(colorPtr, depthPtr, normalPtr, motionPtr, shadowPtr, shadowAddPtr, opaquePtr, ssaoPtr, s_lastW, s_lastH);
            }
        }

        class CaptureFeature : ScriptableRendererFeature
        {
            CapturePass _pass;

            public override void Create()
            {
                _pass = new CapturePass();
            }

            public override void AddRenderPasses(ScriptableRenderer renderer, ref RenderingData renderingData)
            {
                if (renderingData.cameraData.cameraType != CameraType.Game) return;
                _pass.ConfigureInput(
                    ScriptableRenderPassInput.Color |
                    ScriptableRenderPassInput.Depth |
                    ScriptableRenderPassInput.Normal |
                    ScriptableRenderPassInput.Motion);
                renderer.EnqueuePass(_pass);
            }
        }
    }
}