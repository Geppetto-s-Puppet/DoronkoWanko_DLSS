using BepInEx;
using BepInEx.Logging;
using HarmonyLib;
using System;
using System.Runtime.InteropServices;
using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.SceneManagement;

// ─────────────────────────────────────────────────────────────────────

namespace DoronkoWanko_DLSS
{
    [BepInPlugin("com.alex.doronkowankodlss", "DoronkoWanko DLSS", "2.0.3")]
    public class Plugin : BaseUnityPlugin
    {
        internal static ManualLogSource Log;
        void Awake()
        {
            QualitySettings.vSyncCount = 0;
            SceneManager.sceneLoaded += OnSceneLoaded;
            new Harmony("com.alex.doronkowankodlss").PatchAll();

            (Log = Logger).LogInfo("Plugin loaded successfully!");
            Log.LogInfo(
                $"Graphics: {SystemInfo.graphicsDeviceType}" +
                $"Version : {UnityEngine.Application.unityVersion}" +
                $"Pipeline: {UnityEngine.Rendering.GraphicsSettings.currentRenderPipeline.GetType()}"
            );
        }

        internal static DW_DLSS dlss;
        void OnSceneLoaded(Scene scene, LoadSceneMode mode) // ゲームのクリーンアップ後に発火させないと
        {
            if (dlss == null) { DontDestroyOnLoad(dlss = new GameObject("DW_DLSS").AddComponent<DW_DLSS>()); }

            foreach (var cam in Camera.allCameras)
            {
                if (cam.name == "Main Camera") dlss.mainCamera = cam;
                else if (cam.name == "UI Camera") dlss.uiCamera = cam;
                Log.LogInfo($"Scanned: {scene.name}/{cam.name}({cam.depth},{cam.actualRenderingPath})");
            }
        }

        // ──────────────────────────────────────────────────────────────────

        internal class DW_DLSS : MonoBehaviour
        {
            internal static string shaderPath;                   // ファイルパスだけを渡す
            internal Camera uiCamera, mainCamera;                // UIにはシェーダーを適用しない
            internal static bool uKeyWasDown, rKeyWasDown;       // UIトグル(U)とシェーダー読込(R)

            [DllImport("user32.dll")] static extern short GetAsyncKeyState(int vKey);
            void Update() { if (!UnityEngine.Application.isFocused) return; HandleHotkeyInputs(); }

            [DllImport("DW_DLSS_N.dll")] static extern void DW_Init(IntPtr probeTexture);
            void Start() // C#→C++ ダミーテクスチャを渡して、逆引きでUnityデバイスと同期
            {
                var probe = new RenderTexture(4, 4, 0, RenderTextureFormat.ARGB32);
                probe.Create(); DW_Init(probe.GetNativeTexturePtr()); probe.Release();
            }

            [DllImport("DW_DLSS_N.dll")] static extern void DW_Release();
            void OnDestroy() => DW_Release();

            // ────────────────────────────────────────────────────────────────

            void HandleHotkeyInputs()
            {
                bool uDown = (GetAsyncKeyState(0x55) & 0x8000) != 0;
                if (uDown && !uKeyWasDown && uiCamera != null) uiCamera.enabled ^= true;
                uKeyWasDown = uDown;

                bool rDown = (GetAsyncKeyState(0x52) & 0x8000) != 0;
                if (rDown && !rKeyWasDown) { shaderPath = null; BrowseShaderFolderAsync(); }
                rKeyWasDown = rDown;
            }

            private static readonly string pluginDir = System.IO.Path.Combine(
                AppDomain.CurrentDomain.BaseDirectory, "BepInEx", "plugins");
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
            [DllImport("comdlg32.dll", CharSet = CharSet.Unicode)]
            static extern bool GetOpenFileName(ref OPENFILENAME ofn);
            void BrowseShaderFolderAsync()
            {
                var thread = new System.Threading.Thread(() =>
                {
                    var ofn = new OPENFILENAME
                    {
                        lStructSize = Marshal.SizeOf<OPENFILENAME>(),
                        lpstrFile = new string('\0', 256),
                        nMaxFile = 256,
                        lpstrFilter = "HLSL Files\0*.hlsl\0",
                        nFilterIndex = 1,
                        lpstrInitialDir = pluginDir,
                        lpstrTitle = "Choose the shader",
                        Flags = 0x00001000,
                    };
                    if (GetOpenFileName(ref ofn)) Log.LogInfo($"chose: {shaderPath = ofn.lpstrFile}");

                });
                thread.SetApartmentState(System.Threading.ApartmentState.STA);
                thread.IsBackground = true;
                thread.Start();
            }
        }
    }

    // ────────────────────────────────────────────────────────────────────

    [HarmonyPatch(typeof(ScriptableRenderer), "Execute")]
    class ScriptableRenderer_Execute_Patch
    {
        static bool s_initialized = false;
        static readonly (string name, int id)[] s_rtList = new[] {
            ("Color",         Shader.PropertyToID("_CameraColorTexture")),               // [0] 颜色
            ("Opaque",        Shader.PropertyToID("_CameraOpaqueTexture")),              // [1] 不透
            ("Depth",         Shader.PropertyToID("_CameraDepthTexture")),               // [2] 深度
            ("Normals",       Shader.PropertyToID("_CameraNormalsTexture")),             // [3] 法线
            ("SSAO",          Shader.PropertyToID("_ScreenSpaceAOTexture")),             // [4] 环遮
            ("MotionVectors", Shader.PropertyToID("_CameraMotionVectorsTexture")),       // [5] 运动
            ("MainShadow",    Shader.PropertyToID("_MainLightShadowmapTexture")),        // [6] 阴影
            ("AddShadow",     Shader.PropertyToID("_AdditionalLightsShadowmapTexture")), // [7] 附影
            // SRVの上限が8個なので、これより下はスキャン専用
        };

        [DllImport("DW_DLSS_N.dll")] static extern void DW_Update(IntPtr[] texPtrs, int count);
        static void Postfix(ScriptableRenderContext context, ref RenderingData renderingData)
        {
            var cam = renderingData.cameraData.camera;
            if (cam != Plugin.dlss.mainCamera) return;
            Plugin.Log.LogInfo("========================");
            cam.depthTextureMode |= DepthTextureMode.Depth;

            if (!s_initialized)
            {
                s_initialized = true;


                //// ここでなんとかしてOpaque、法線、モーションベクターを有効化したい

                //try
                //{
                //    var pl = GraphicsSettings.currentRenderPipeline;
                //    if (pl == null) return;
                //    var bf = BindingFlags.NonPublic | BindingFlags.Instance;
                //    var type = pl.GetType();

                //    // Opaque有効化
                //    var fi = type.GetField("m_RequireOpaqueTexture", bf);
                //    if (fi?.FieldType == typeof(bool)) fi.SetValue(pl, true);

                //    // bool型でopaque/normal/depth/motionを含むフィールドを全部trueに
                //    foreach (var f in type.GetFields(bf))
                //    {
                //        if (f.FieldType != typeof(bool)) continue;
                //        string n = f.Name.ToLower();
                //        if (n.Contains("opaque") || n.Contains("normal") ||
                //            n.Contains("depth") || n.Contains("motion"))
                //        {
                //            if (!(bool)f.GetValue(pl))
                //            {
                //                f.SetValue(pl, true);
                //                Plugin.Log.LogInfo($"[URP] {f.Name}: false → true");
                //            }
                //        }
                //    }
                //}
                //catch (Exception e) { Plugin.Log.LogWarning($"[Patch] URP patch: {e.Message}"); }

                //cam.depthTextureMode |=
                //    DepthTextureMode.Depth |
                //    DepthTextureMode.DepthNormals |
                //    DepthTextureMode.MotionVectors;
            }

            IntPtr[] rtPtrs = new IntPtr[s_rtList.Length];
            for (int i = 0; i < s_rtList.Length; i++)
            {
                var tex = Shader.GetGlobalTexture(s_rtList[i].id) as RenderTexture;
                if (tex != null && tex.IsCreated()) rtPtrs[i] = tex.GetNativeTexturePtr();
                Plugin.Log.LogInfo($"RenderTarget{i}: {s_rtList[i].name,-12} = {rtPtrs[i]} {tex?.width}x{tex?.height}");
                // {tex?.format}はあくまでUnityのEnumだから、実際のDX12フォーマットはID3D12Resource*からGetDesc().Formatしてね
            }

            DW_Update(rtPtrs, 8);

        }
    }
}




// ここから下は全部nullった
//"m_ActiveCameraDepthAttachment",   // rts[1] 深度バッファ
//"m_CameraDepthAttachment",         // rts[2] 深度バッファ2
//"m_DepthTexture",                  // rts[3] 深度テクスチャ
//"m_OpaqueColor",                   // rts[4] Opaque
//"_CameraDepthTexture",             // rts[5] 深度(Global)
//"",      // rts[6] 法線+深度
//"_CameraMotionVectorsTexture",     // rts[7] モーションベクター
