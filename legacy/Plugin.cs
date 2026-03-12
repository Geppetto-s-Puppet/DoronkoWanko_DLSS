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
    [BepInPlugin("com.alex.doronkowankodlss", "DoronkoWanko DLSS", "1.2.0")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;

        void Awake()
        {
            SceneManager.sceneLoaded += OnSceneLoaded;
            (Log = Logger).LogInfo("Plugin loaded!");
        }

        void OnSceneLoaded(Scene scene, LoadSceneMode mode)
        {
            if (FindObjectOfType<DW_DLSS>() != null) return;
            var dlss = new GameObject("DW_DLSS").AddComponent<DW_DLSS>();
            DontDestroyOnLoad(dlss);
            foreach (var cam in Camera.allCameras)
            {
                Log.LogInfo($"Camera: {scene.name}/{cam.name}({cam.depth})");
                switch (cam.name)
                {
                    case "UI Camera": dlss.uiCamera = cam; break;
                    case "Main Camera": dlss.mainCamera = cam; break;
                }
            }
        }

        // ────────────────────────────────────────────────────────────────────
        public class DW_DLSS : MonoBehaviour
        {
            internal Camera uiCamera;
            internal Camera mainCamera;
            string shaderPath = null;
            bool uKeyWasDown, rKeyWasDown;

            [DllImport("DW_DLSS_N.dll")]
            static extern void DW_Init(IntPtr sampleResourcePtr);

            // shaderPath=nullのときは何も読まない、visible=falseのときは非表示
            [DllImport("DW_DLSS_N.dll", CharSet = CharSet.Unicode)]
            static extern void DW_Update(string shaderPath, IntPtr[] textures, [MarshalAs(UnmanagedType.Bool)] bool visible, int w, int h);

            [DllImport("DW_DLSS_N.dll")]
            static extern void DW_Draw();

            [DllImport("DW_DLSS_N.dll")]
            static extern void DW_Release();

            [DllImport("comdlg32.dll", CharSet = CharSet.Unicode)]
            static extern bool GetOpenFileName(ref OPENFILENAME ofn);

            [DllImport("user32.dll")]
            static extern short GetAsyncKeyState(int vKey);

            [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
            struct OPENFILENAME
            {
                public int lStructSize;
                public IntPtr hwndOwner, hInstance;
                public string lpstrFilter, lpstrCustomFilter;
                public int nMaxCustFilter, nFilterIndex;
                public string lpstrFile;
                public int nMaxFile;
                public string lpstrFileTitle;
                public int nMaxFileTitle;
                public string lpstrInitialDir, lpstrTitle;
                public int Flags;
                public short nFileOffset, nFileExtension;
                public string lpstrDefExt;
                public IntPtr lCustData, lpfnHook;
                public string lpTemplateName;
            }

            void Awake()
            {
                StartCoroutine(InitAndCleanup());

                CapturePass.OnCapture = (colorPtr, depthPtr, normalPtr, motionPtr, shadowPtr, shadowAddPtr, opaquePtr, ssaoPtr, w, h) =>
                {
                    //var ptrs = new IntPtr[8] { colorPtr, depthPtr, normalPtr, motionPtr, shadowPtr, shadowAddPtr, opaquePtr, ssaoPtr };
                    bool visible = Application.isFocused && shaderPath != null;
                    //DW_Update(shaderPath, new IntPtr[8], visible, w, h);
                    //DW_Update(shaderPath, ptrs, visible, w, h);

                    var ptrs = new IntPtr[8];
                    ptrs[0] = colorPtr;
                    ptrs[1] = depthPtr;
                    ptrs[2] = normalPtr;
                    //ptrs[3] = motionPtr;
                    DW_Update(shaderPath, ptrs, visible, w, h);


                };

                StartCoroutine(EndOfFrameLoop());
                StartCoroutine(TryAddFeatureDelayed());
            }

            System.Collections.IEnumerator InitAndCleanup()
            {
                // UnityのD3D12Resourceポインタを渡してデバイスを共有させる
                var probe = new RenderTexture(4, 4, 0, RenderTextureFormat.ARGB32);
                probe.Create();
                DW_Init(probe.GetNativeTexturePtr());
                yield return null;
                probe.Release();
                Destroy(probe);
            }

            System.Collections.IEnumerator TryAddFeatureDelayed()
            {
                yield return null;  // URPが準備完了するまで1フレーム待つ
                TryAddFeature();
            }

            void Update()
            {
                if (!Application.isFocused) return;

                // U: UIカメラのトグル
                bool uDown = (GetAsyncKeyState(0x55) & 0x8000) != 0;
                if (uDown && !uKeyWasDown && uiCamera != null)
                    uiCamera.enabled = !uiCamera.enabled;
                uKeyWasDown = uDown;

                // R: ファイルダイアログでシェーダーを選択
                // ダイアログ中はshaderPath=nullになるのでDW_UpdateがvisibleをfalseにしてC++側が隠す
                bool rDown = (GetAsyncKeyState(0x52) & 0x8000) != 0;
                if (rDown && !rKeyWasDown)
                {
                    shaderPath = null;
                    var thread = new Thread(() =>
                    {
                        var ofn = new OPENFILENAME
                        {
                            lStructSize = Marshal.SizeOf<OPENFILENAME>(),
                            lpstrFile = new string('\0', 256),
                            nMaxFile = 256,
                            lpstrFilter = "HLSL Files\0*.hlsl\0",
                            nFilterIndex = 1,
                            lpstrInitialDir = System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "BepInEx", "plugins"),
                            lpstrTitle = "シェーダーを選択",
                            Flags = 0x00001000,
                        };
                        if (GetOpenFileName(ref ofn))
                        {
                            shaderPath = ofn.lpstrFile;
                            Plugin.Log.LogInfo($"Shader: {shaderPath}");
                        }
                    });
                    thread.SetApartmentState(ApartmentState.STA);
                    thread.IsBackground = true;
                    thread.Start();
                }
                rKeyWasDown = rDown;
            }

            System.Collections.IEnumerator EndOfFrameLoop()
            {
                while (true)
                {
                    yield return new WaitForEndOfFrame();
                    CapturePass.ReadAndSend();  // テクスチャ取得 → DW_Update
                    DW_Draw();                  // DW_Updateの後に必ず呼ぶ
                }
            }

            void OnDestroy() => DW_Release();

            internal void TryAddFeature()
            {
                var pl = GraphicsSettings.currentRenderPipeline as UniversalRenderPipelineAsset;
                if (pl == null) { Plugin.Log.LogInfo("TryAddFeature: no URP asset"); return; }

                var bf = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;

                // RendererDataリストの全エントリに注入（シーン切り替え後も有効）
                var dataList = pl.GetType().GetField("m_RendererDataList", bf)?.GetValue(pl) as UnityEngine.Object[];
                if (dataList != null)
                {
                    foreach (var obj in dataList)
                    {
                        var rd = obj as ScriptableRendererData;
                        if (rd == null) continue;

                        System.Reflection.FieldInfo fi = null;
                        for (var t = rd.GetType(); t != null; t = t.BaseType)
                        {
                            fi = t.GetField("m_RendererFeatures", bf);
                            if (fi != null) break;
                        }
                        if (fi == null) continue;

                        var features = fi.GetValue(rd) as System.Collections.Generic.List<ScriptableRendererFeature>;
                        if (features == null) continue;

                        bool exists = false;
                        foreach (var f in features) if (f is CaptureFeature) { exists = true; break; }
                        if (exists) continue;

                        var feat = ScriptableObject.CreateInstance<CaptureFeature>();
                        feat.name = "DW_Capture";
                        DontDestroyOnLoad(feat);
                        feat.Create();
                        features.Add(feat);
                        Plugin.Log.LogInfo($"CaptureFeature → {rd.name}");
                    }
                }

                // 今動いているRendererにも直接注入（即座に有効化）
                var live = pl.scriptableRenderer;
                if (live == null) { Plugin.Log.LogInfo("TryAddFeature: no live renderer"); return; }

                System.Reflection.FieldInfo liveField = null;
                for (var t = live.GetType(); t != null; t = t.BaseType)
                {
                    liveField = t.GetField("m_RendererFeatures", bf);
                    if (liveField != null) break;
                }
                if (liveField == null) { Plugin.Log.LogInfo("TryAddFeature: field not found"); return; }

                var liveFeats = liveField.GetValue(live) as System.Collections.Generic.List<ScriptableRendererFeature>;
                if (liveFeats == null) { Plugin.Log.LogInfo("TryAddFeature: list null"); return; }

                foreach (var f in liveFeats) if (f is CaptureFeature) { Plugin.Log.LogInfo("TryAddFeature: already injected"); return; }

                var liveFeat = ScriptableObject.CreateInstance<CaptureFeature>();
                liveFeat.name = "DW_Capture";
                DontDestroyOnLoad(liveFeat);
                liveFeat.Create();
                liveFeats.Add(liveFeat);
                Plugin.Log.LogInfo($"CaptureFeature → live:{live.GetType().Name}");
            }
        }

        // ────────────────────────────────────────────────────────────────────
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

            static volatile int s_executed = 0;
            static volatile int s_lastW = 0;
            static volatile int s_lastH = 0;

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
                Interlocked.Exchange(ref s_executed, 1);
            }

            public static void ReadAndSend()
            {
                if (Interlocked.Exchange(ref s_executed, 0) == 0) return;

                IntPtr Get(int id)
                {
                    var t = Shader.GetGlobalTexture(id);
                    return t != null ? t.GetNativeTexturePtr() : IntPtr.Zero;
                }

                IntPtr colorPtr = Get(s_idColor);
                IntPtr depthPtr = Get(s_idDepth);
                Plugin.Log.LogInfo($"color={colorPtr} depth={depthPtr}");  // ← 追加

                OnCapture?.Invoke(
                    Get(s_idColor), Get(s_idDepth), Get(s_idNormal), Get(s_idMotion),
                    Get(s_idShadow), Get(s_idShadowAdd), Get(s_idOpaque), Get(s_idSSAO),
                    s_lastW, s_lastH);
            }
        }

        // ────────────────────────────────────────────────────────────────────
        class CaptureFeature : ScriptableRendererFeature
        {
            CapturePass _pass;

            public override void Create() => _pass = new CapturePass();

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